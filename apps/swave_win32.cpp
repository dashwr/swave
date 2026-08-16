#ifdef _WIN32

#include "core/backend/video_backends.hpp"
#include "core/pipeline/processing_pipeline.hpp"
#include "core/source/synthetic_source.hpp"
#include "platform/windows/window_capture.hpp"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

using namespace swave::core;

constexpr UINT kTimerId = 1;
constexpr UINT kTimerPeriodMs = 16;

HWND g_content_window{};
HWND g_settings_window{};
FramePacket g_current_frame;
std::unique_ptr<SyntheticSource> g_source;
std::unique_ptr<swave::platform::WindowCaptureSource> g_capture;
std::unique_ptr<ProcessingPipeline> g_pipeline;
HWND g_title_edit{};
bool g_using_capture{};
std::wstring g_provider_label = L"fake";

ModelManifest make_manifest(ModelKind kind, const char* id, double native_scale, bool timestep) {
    ModelManifest manifest;
    manifest.id = id;
    manifest.kind = kind;
    manifest.model_path = std::string{"models/"} + id + ".onnx";
    manifest.native_scale = native_scale;
    manifest.min_scale = kind == ModelKind::upscaler ? 1.1 : 1.0;
    manifest.max_scale = kind == ModelKind::upscaler ? 5.0 : 1.0;
    manifest.arbitrary_timestep = timestep;
    return manifest;
}

struct AppOptions {
    InferenceProvider provider{InferenceProvider::fake};
    std::optional<std::filesystem::path> upscaler_manifest;
    std::optional<std::filesystem::path> interpolator_manifest;
};

AppOptions parse_options(PWSTR command_line) {
    AppOptions options;
    int argument_count = 0;
    auto* arguments = CommandLineToArgvW(command_line, &argument_count);
    if (!arguments) {
        return options;
    }
    for (int index = 0; index < argument_count; ++index) {
        const std::wstring argument = arguments[index];
        if (argument == L"--provider" && index + 1 < argument_count) {
            const std::wstring provider = arguments[++index];
            if (provider == L"tensorrt") options.provider = InferenceProvider::tensorrt;
            else if (provider == L"cuda") options.provider = InferenceProvider::cuda;
        } else if (argument == L"--upscaler-manifest" && index + 1 < argument_count) {
            options.upscaler_manifest = std::filesystem::path(arguments[++index]);
        } else if (argument == L"--interpolator-manifest" && index + 1 < argument_count) {
            options.interpolator_manifest = std::filesystem::path(arguments[++index]);
        }
    }
    LocalFree(arguments);
    return options;
}

std::optional<ModelManifest> load_or_default(
    const std::optional<std::filesystem::path>& path,
    ModelKind kind,
    const char* id,
    double native_scale,
    bool timestep,
    std::string& error) {
    if (path) {
        return ModelManifest::load_file(*path, error);
    }
    auto manifest = make_manifest(kind, id, native_scale, timestep);
    if (!manifest.validate(error)) {
        return std::nullopt;
    }
    return manifest;
}

void draw_text(HDC dc, RECT rect, const wchar_t* text, COLORREF color, UINT format = DT_LEFT) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rect, format | DT_NOPREFIX);
}

void paint_content(HDC dc, const RECT& client) {
    const auto background = CreateSolidBrush(RGB(23, 23, 23));
    FillRect(dc, &client, background);
    DeleteObject(background);

    if (!g_current_frame.surface.bytes) {
        draw_text(dc, client, L"sWAVe\naguardando fonte", RGB(242, 242, 242), DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        return;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(g_current_frame.surface.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(g_current_frame.surface.height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(
        dc,
        0,
        0,
        client.right - client.left,
        client.bottom - client.top,
        0,
        0,
        static_cast<int>(g_current_frame.surface.width),
        static_cast<int>(g_current_frame.surface.height),
        g_current_frame.surface.bytes->data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY);
}

void paint_settings(HDC dc, const RECT& client) {
    const auto background = CreateSolidBrush(RGB(23, 23, 23));
    FillRect(dc, &client, background);
    DeleteObject(background);

    RECT title{24, 20, client.right - 24, 52};
    draw_text(dc, title, L"sWAVe  /  settings", RGB(242, 242, 242), DT_LEFT);
    RECT status{24, 72, client.right - 24, 104};
    const wchar_t* source = g_using_capture ? L"WGC window" : L"synthetic source";
    std::wstring status_text = L"fonte: ";
    status_text += source;
    status_text += L"   provider: ";
    status_text += g_provider_label;
    status_text += L"   estado: running";
    draw_text(dc, status, status_text.c_str(), RGB(196, 196, 196));
    RECT controls{24, 132, client.right - 24, 260};
    draw_text(dc, controls,
        L"reprodução\n\n  preset       balanced\n  escala       2.0x\n  interpolação RIFE 4.25\n  saída        60 fps",
        RGB(196, 196, 196));
    RECT footer{24, client.bottom - 48, client.right - 24, client.bottom - 20};
    draw_text(dc, footer, L"WGC: informe parte do título da janela e pressione capturar", RGB(142, 142, 142));
}

struct WindowSearch {
    std::wstring needle;
    HWND result{};
};

BOOL CALLBACK find_window(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    if (!IsWindowVisible(window) || window == g_content_window || window == g_settings_window) {
        return TRUE;
    }
    wchar_t title[512]{};
    GetWindowTextW(window, title, ARRAYSIZE(title));
    if (search.needle.empty() || std::wstring(title).find(search.needle) != std::wstring::npos) {
        search.result = window;
        return FALSE;
    }
    return TRUE;
}

void start_window_capture() {
    wchar_t title[512]{};
    GetWindowTextW(g_title_edit, title, ARRAYSIZE(title));
    WindowSearch search{title};
    EnumWindows(find_window, reinterpret_cast<LPARAM>(&search));
    if (!search.result) {
        MessageBoxW(g_settings_window, L"nenhuma janela visível corresponde ao título", L"sWAVe", MB_ICONWARNING);
        return;
    }

    auto capture = std::make_unique<swave::platform::WindowCaptureSource>(
        swave::platform::WindowCaptureConfig{search.result, 4});
    std::string error;
    if (!capture->start(error)) {
        MessageBoxA(g_settings_window, error.c_str(), "sWAVe · WGC", MB_ICONERROR);
        return;
    }
    g_capture = std::move(capture);
    g_using_capture = true;
    InvalidateRect(g_settings_window, nullptr, FALSE);
}

void tick() {
    if (!g_pipeline || !g_source) return;
    FramePacket input;
    if (g_using_capture) {
        if (!g_capture || !g_capture->try_receive(input)) return;
    } else {
        input = g_source->next();
    }
    if (!g_pipeline->submit(std::move(input)) || !g_pipeline->process_one()) return;
    FramePacket output;
    while (g_pipeline->receive(output)) {
        g_current_frame = std::move(output);
    }
    InvalidateRect(g_content_window, nullptr, FALSE);
    InvalidateRect(g_settings_window, nullptr, FALSE);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wparam) == 1001) start_window_capture();
        return 0;
    case WM_TIMER:
        if (wparam == kTimerId) tick();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        if (window == g_content_window) paint_content(dc, client);
        else paint_settings(dc, client);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        if (window == g_content_window) {
            KillTimer(window, kTimerId);
            PostQuitMessage(0);
        }
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

bool register_class(HINSTANCE instance, const wchar_t* name) {
    WNDCLASSW window_class{};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = name;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    return RegisterClassW(&window_class) != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line, int show_command) {
    std::string error;
    const auto options = parse_options(command_line);
    const auto upscaler_manifest = load_or_default(
        options.upscaler_manifest, ModelKind::upscaler, "srvgv-test", 4.0, false, error);
    const auto interpolator_manifest = load_or_default(
        options.interpolator_manifest, ModelKind::interpolator, "rife-test", 1.0, true, error);
    if (!upscaler_manifest || !interpolator_manifest) {
        MessageBoxA(nullptr, error.c_str(), "sWAVe · manifesto", MB_ICONERROR);
        return 1;
    }

    std::shared_ptr<IUpscalerBackend> upscaler;
    std::shared_ptr<IInterpolatorBackend> interpolator;
    InferenceOptions inference_options;
    inference_options.provider = options.provider;
    if (options.provider == InferenceProvider::fake) {
        upscaler = std::make_shared<FakeUpscalerBackend>();
        interpolator = std::make_shared<FakeInterpolatorBackend>();
        g_provider_label = L"fake";
    } else {
        upscaler = std::make_shared<OnnxUpscalerBackend>();
        interpolator = std::make_shared<OnnxInterpolatorBackend>();
        g_provider_label = options.provider == InferenceProvider::tensorrt ? L"tensorrt" : L"cuda";
    }
    auto upscaler_session = options.provider == InferenceProvider::fake
        ? std::shared_ptr<IInferenceSession>{std::make_shared<FakeInferenceSession>()}
        : make_inference_session(inference_options, error);
    auto interpolator_session = options.provider == InferenceProvider::fake
        ? std::shared_ptr<IInferenceSession>{std::make_shared<FakeInferenceSession>()}
        : make_inference_session(inference_options, error);
    if (!upscaler_session || !interpolator_session ||
        !upscaler->initialize(*upscaler_manifest, upscaler_session, inference_options, error) ||
        !interpolator->initialize(*interpolator_manifest, interpolator_session, inference_options, error)) {
        MessageBoxA(nullptr, error.c_str(), "sWAVe", MB_ICONERROR);
        return 1;
    }
    g_pipeline = std::make_unique<ProcessingPipeline>(
        ProcessingPipelineConfig{8, 32, true}, upscaler, interpolator);
    g_pipeline->start();
    g_source = std::make_unique<SyntheticSource>(SyntheticSourceConfig{{640, 360}, 30});

    constexpr auto content_class = L"swave_content";
    constexpr auto settings_class = L"swave_settings";
    if (!register_class(instance, content_class) || !register_class(instance, settings_class)) return 1;
    g_content_window = CreateWindowExW(
        0, content_class, L"sWAVe · content", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 540, nullptr, nullptr, instance, nullptr);
    g_settings_window = CreateWindowExW(
        0, settings_class, L"sWAVe · settings", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 420, nullptr, nullptr, instance, nullptr);
    if (!g_content_window || !g_settings_window) return 1;
    ShowWindow(g_content_window, show_command);
    ShowWindow(g_settings_window, SW_SHOW);
    g_title_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"Chrome", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        24, 280, 360, 26, g_settings_window, reinterpret_cast<HMENU>(1000), instance, nullptr);
    CreateWindowW(
        L"BUTTON", L"capturar janela", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        396, 280, 150, 26, g_settings_window, reinterpret_cast<HMENU>(1001), instance, nullptr);
    SetTimer(g_content_window, kTimerId, kTimerPeriodMs, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    g_pipeline->stop();
    return static_cast<int>(message.wParam);
}

#endif
