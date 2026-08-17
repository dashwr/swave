#ifdef _WIN32

#include "platform/windows/window_capture.hpp"

#include "core/queue/bounded_queue.hpp"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <limits>
#include <mutex>
#include <ratio>
#include <string>
#include <atomic>
#include <utility>
#include <vector>

namespace swave::platform {
namespace {

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

} // namespace

struct WindowCaptureSource::Impl {
    explicit Impl(WindowCaptureConfig capture_config)
        : config(capture_config), frames(capture_config.queue_capacity) {}

    WindowCaptureConfig config;
    core::BoundedQueue<core::FramePacket> frames;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> staging;
    IDirect3DDevice winrt_device{nullptr};
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool frame_pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    winrt::event_token frame_arrived{};
    std::uint64_t sequence{};
    std::atomic_bool running{};
    mutable std::mutex callback_mutex;
    mutable std::mutex lifecycle_mutex;
    mutable std::mutex error_mutex;
    std::string error;

    void set_error(std::string value) {
        std::lock_guard lock(error_mutex);
        error = std::move(value);
        running.store(false, std::memory_order_release);
    }
};

WindowCaptureSource::WindowCaptureSource(WindowCaptureConfig config)
    : impl_(std::make_shared<Impl>(config)) {}

WindowCaptureSource::~WindowCaptureSource() {
    stop();
}

bool WindowCaptureSource::start(std::string& error) {
    std::unique_lock lifecycle_lock(impl_->lifecycle_mutex);
    if (impl_->running.load(std::memory_order_acquire)) return true;
    lifecycle_lock.unlock();
    stop();
    lifecycle_lock.lock();
    if (!impl_->config.window || !IsWindow(static_cast<HWND>(impl_->config.window))) {
        error = "window capture requires a valid HWND";
        return false;
    }
    impl_->frames.reset();
    impl_->sequence = 0;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL feature_level{};
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0,
        };
        HRESULT device_result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels,
            ARRAYSIZE(levels),
            D3D11_SDK_VERSION,
            impl_->device.GetAddressOf(),
            &feature_level,
            impl_->context.GetAddressOf());
        if (device_result == E_INVALIDARG) {
            impl_->device.Reset();
            impl_->context.Reset();
            const D3D_FEATURE_LEVEL fallback_levels[] = {
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_0,
            };
            device_result = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                flags,
                fallback_levels,
                ARRAYSIZE(fallback_levels),
                D3D11_SDK_VERSION,
                impl_->device.GetAddressOf(),
                &feature_level,
                impl_->context.GetAddressOf());
        }
        winrt::check_hresult(device_result);

        ComPtr<IDXGIDevice> dxgi_device;
        winrt::check_hresult(impl_->device.As(&dxgi_device));
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
            dxgi_device.Get(), reinterpret_cast<IInspectable**>(winrt::put_abi(impl_->winrt_device))));

        auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(factory->CreateForWindow(
            static_cast<HWND>(impl_->config.window),
            winrt::guid_of<GraphicsCaptureItem>(),
            winrt::put_abi(impl_->item)));

        const auto size = impl_->item.Size();
        impl_->frame_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            impl_->winrt_device,
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            size);
        impl_->session = impl_->frame_pool.CreateCaptureSession(impl_->item);
        impl_->frame_arrived = impl_->frame_pool.FrameArrived(
            [state = impl_](auto const& pool, auto const&) {
                std::lock_guard callback_lock(state->callback_mutex);
                try {
                    auto frame = pool.TryGetNextFrame();
                    if (!frame || !state->running.load(std::memory_order_acquire)) return;
                    auto access = frame.Surface().as<::IDirect3DDxgiInterfaceAccess>();
                    ComPtr<ID3D11Texture2D> source;
                    winrt::check_hresult(access->GetInterface(
                        __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(source.GetAddressOf())));

                    D3D11_TEXTURE2D_DESC description{};
                    source->GetDesc(&description);
                    if (description.Width == 0 || description.Height == 0 ||
                        description.Width > static_cast<UINT>(std::numeric_limits<std::int32_t>::max()) ||
                        description.Height > static_cast<UINT>(std::numeric_limits<std::int32_t>::max())) {
                        return;
                    }
                    const auto content_size = frame.ContentSize();
                    if (content_size.Width <= 0 || content_size.Height <= 0) return;
                    if (content_size.Width != static_cast<std::int32_t>(description.Width) ||
                        content_size.Height != static_cast<std::int32_t>(description.Height)) {
                        state->frame_pool.Recreate(
                            state->winrt_device,
                            DirectXPixelFormat::B8G8R8A8UIntNormalized,
                            2,
                            content_size);
                    }
                    if (!state->staging || description.Width != [&] {
                        D3D11_TEXTURE2D_DESC current{};
                        if (state->staging) state->staging->GetDesc(&current);
                        return current.Width;
                    }() || description.Height != [&] {
                        D3D11_TEXTURE2D_DESC current{};
                        if (state->staging) state->staging->GetDesc(&current);
                        return current.Height;
                    }()) {
                        D3D11_TEXTURE2D_DESC staging_description = description;
                        staging_description.Usage = D3D11_USAGE_STAGING;
                        staging_description.BindFlags = 0;
                        staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                        staging_description.MiscFlags = 0;
                        state->staging.Reset();
                        winrt::check_hresult(state->device->CreateTexture2D(
                            &staging_description, nullptr, state->staging.GetAddressOf()));
                    }
                    state->context->CopyResource(state->staging.Get(), source.Get());
                    state->context->Flush();
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    winrt::check_hresult(state->context->Map(
                        state->staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
                    struct MapGuard {
                        ID3D11DeviceContext* context;
                        ID3D11Resource* resource;
                        ~MapGuard() { context->Unmap(resource, 0); }
                    } map_guard{state->context.Get(), state->staging.Get()};
                    const auto width = static_cast<std::size_t>(description.Width);
                    const auto height = static_cast<std::size_t>(description.Height);
                    if (width > std::numeric_limits<std::size_t>::max() / 4 ||
                        height > std::numeric_limits<std::size_t>::max() / (width * 4)) {
                        state->set_error("captured frame dimensions overflow buffer size");
                        return;
                    }
                    auto bytes = std::make_shared<std::vector<std::byte>>(width * height * 4);
                    const auto row_bytes = static_cast<std::size_t>(description.Width) * 4;
                    for (std::uint32_t row = 0; row < description.Height; ++row) {
                        std::memcpy(
                            bytes->data() + row * row_bytes,
                            static_cast<const std::byte*>(mapped.pData) + row * mapped.RowPitch,
                            row_bytes);
                    }
                    core::FramePacket packet;
                    packet.surface.format = core::PixelFormat::bgra8;
                    packet.surface.width = description.Width;
                    packet.surface.height = description.Height;
                    packet.surface.bytes = std::move(bytes);
                    packet.media_pts = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>(
                            frame.SystemRelativeTime().count()));
                    packet.capture_time = std::chrono::steady_clock::now();
                    packet.sequence = state->sequence++;
                    state->frames.try_push(std::move(packet));
                } catch (const winrt::hresult_error& exception) {
                    state->set_error(winrt::to_string(exception.message()));
                } catch (const std::exception& exception) {
                    state->set_error(exception.what());
                } catch (...) {
                    state->set_error("unknown exception in WGC frame callback");
                }
            });
        impl_->running.store(true, std::memory_order_release);
        impl_->session.StartCapture();
        return true;
    } catch (const winrt::hresult_error& exception) {
        error = winrt::to_string(exception.message());
        lifecycle_lock.unlock();
        stop();
        return false;
    } catch (const std::exception& exception) {
        error = exception.what();
        lifecycle_lock.unlock();
        stop();
        return false;
    } catch (...) {
        error = "unknown exception while starting WGC";
        lifecycle_lock.unlock();
        stop();
        return false;
    }
}

void WindowCaptureSource::stop() noexcept {
    if (!impl_) return;
    std::lock_guard lifecycle_lock(impl_->lifecycle_mutex);
    impl_->running.store(false, std::memory_order_release);
    try {
        if (impl_->frame_pool) impl_->frame_pool.FrameArrived(impl_->frame_arrived);
    } catch (...) {
    }
    {
        std::lock_guard callback_lock(impl_->callback_mutex);
    }
    try {
        if (impl_->session) impl_->session.Close();
        if (impl_->frame_pool) impl_->frame_pool.Close();
    } catch (...) {
    }
    impl_->session = nullptr;
    impl_->frame_pool = nullptr;
    impl_->item = nullptr;
    impl_->winrt_device = nullptr;
    impl_->staging.Reset();
    impl_->context.Reset();
    impl_->device.Reset();
    impl_->frames.reset();
    impl_->sequence = 0;
}

bool WindowCaptureSource::try_receive(core::FramePacket& frame) {
    return impl_->frames.try_pop(frame);
}

bool WindowCaptureSource::running() const noexcept {
    return impl_->running.load(std::memory_order_acquire);
}

std::string WindowCaptureSource::last_error() const {
    std::lock_guard lock(impl_->error_mutex);
    return impl_->error;
}

} // namespace swave::platform

#endif
