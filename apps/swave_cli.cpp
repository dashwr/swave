#include "core/backend/video_backends.hpp"
#include "core/pipeline/processing_pipeline.hpp"
#include "core/source/synthetic_source.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>

namespace {

struct Arguments {
    std::size_t frames{30};
    unsigned fps{30};
    std::uint32_t width{640};
    std::uint32_t height{360};
    bool interpolation{true};
    swave::core::InferenceProvider provider{swave::core::InferenceProvider::fake};
    std::filesystem::path upscaler_manifest;
    std::filesystem::path interpolator_manifest;
};

bool parse_unsigned(std::string_view text, unsigned& value) {
    char* end{};
    const auto parsed = std::strtoul(text.data(), &end, 10);
    if (end != text.data() + text.size() || parsed == 0) {
        return false;
    }
    value = static_cast<unsigned>(parsed);
    return true;
}

bool parse_size(std::string_view text, std::size_t& value) {
    unsigned parsed{};
    if (!parse_unsigned(text, parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool parse_arguments(int argc, char** argv, Arguments& arguments) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--no-interpolation") {
            arguments.interpolation = false;
            continue;
        }
        if (index + 1 >= argc) {
            return false;
        }
        const std::string_view value = argv[++index];
        if (argument == "--frames" && !parse_size(value, arguments.frames)) return false;
        if (argument == "--fps" && !parse_unsigned(value, arguments.fps)) return false;
        if (argument == "--width" && !parse_unsigned(value, arguments.width)) return false;
        if (argument == "--height" && !parse_unsigned(value, arguments.height)) return false;
        if (argument == "--provider") {
            if (value == "fake") arguments.provider = swave::core::InferenceProvider::fake;
            else if (value == "tensorrt") arguments.provider = swave::core::InferenceProvider::tensorrt;
            else if (value == "cuda") arguments.provider = swave::core::InferenceProvider::cuda;
            else return false;
        }
        if (argument == "--upscaler-manifest") arguments.upscaler_manifest = value;
        if (argument == "--interpolator-manifest") arguments.interpolator_manifest = value;
    }
    return arguments.frames != 0 && arguments.width != 0 && arguments.height != 0;
}

swave::core::ModelManifest manifest(
    swave::core::ModelKind kind,
    std::string id,
    double native_scale,
    bool arbitrary_timestep) {
    swave::core::ModelManifest result;
    result.id = std::move(id);
    result.kind = kind;
    result.model_path = "models/" + result.id + ".onnx";
    result.native_scale = native_scale;
    result.min_scale = kind == swave::core::ModelKind::upscaler ? 1.1 : 1.0;
    result.max_scale = kind == swave::core::ModelKind::upscaler ? 5.0 : 1.0;
    result.arbitrary_timestep = arbitrary_timestep;
    result.input_names = kind == swave::core::ModelKind::interpolator
        ? "frame0,frame1,timestep" : "input";
    result.output_names = "output";
    return result;
}

} // namespace

int main(int argc, char** argv) {
    Arguments arguments;
    if (!parse_arguments(argc, argv, arguments)) {
        std::cerr << "usage: swave_cli [--frames N] [--fps N] [--width N] [--height N] [--provider fake|tensorrt|cuda] [--upscaler-manifest FILE] [--interpolator-manifest FILE] [--no-interpolation]\n";
        return 2;
    }

    std::string error;
    auto upscaler_manifest = manifest(
        swave::core::ModelKind::upscaler, "srvgv-test", 4.0, false);
    auto interpolator_manifest = manifest(
        swave::core::ModelKind::interpolator, "rife-test", 1.0, true);
    if (!arguments.upscaler_manifest.empty()) {
        auto loaded = swave::core::ModelManifest::load_file(arguments.upscaler_manifest, error);
        if (!loaded) {
            std::cerr << "upscaler manifest failed: " << error << '\n';
            return 1;
        }
        upscaler_manifest = std::move(*loaded);
    }
    if (!arguments.interpolator_manifest.empty()) {
        auto loaded = swave::core::ModelManifest::load_file(arguments.interpolator_manifest, error);
        if (!loaded) {
            std::cerr << "interpolator manifest failed: " << error << '\n';
            return 1;
        }
        interpolator_manifest = std::move(*loaded);
    }

    std::shared_ptr<swave::core::IUpscalerBackend> upscaler;
    std::shared_ptr<swave::core::IInterpolatorBackend> interpolator;
    swave::core::InferenceOptions inference_options;
    inference_options.provider = arguments.provider;
    if (arguments.provider == swave::core::InferenceProvider::fake) {
        upscaler = std::make_shared<swave::core::FakeUpscalerBackend>();
        interpolator = std::make_shared<swave::core::FakeInterpolatorBackend>();
    } else {
        upscaler = std::make_shared<swave::core::OnnxUpscalerBackend>();
        interpolator = std::make_shared<swave::core::OnnxInterpolatorBackend>();
    }
    auto upscaler_session = arguments.provider == swave::core::InferenceProvider::fake
        ? std::shared_ptr<swave::core::IInferenceSession>{std::make_shared<swave::core::FakeInferenceSession>()}
        : swave::core::make_inference_session(inference_options, error);
    if (!upscaler_session || !upscaler->initialize(upscaler_manifest, upscaler_session, inference_options, error)) {
        std::cerr << "upscaler initialization failed: " << error << '\n';
        return 1;
    }
    auto interpolator_session = arguments.provider == swave::core::InferenceProvider::fake
        ? std::shared_ptr<swave::core::IInferenceSession>{std::make_shared<swave::core::FakeInferenceSession>()}
        : swave::core::make_inference_session(inference_options, error);
    if (arguments.interpolation && (!interpolator_session || !interpolator->initialize(
            interpolator_manifest, interpolator_session, inference_options, error))) {
        std::cerr << "interpolator initialization failed: " << error << '\n';
        return 1;
    }

    swave::core::ProcessingPipeline pipeline(
        {4, 16, arguments.interpolation}, upscaler, interpolator);
    pipeline.start();
    if (pipeline.state() == swave::core::PipelineState::error) {
        std::cerr << "pipeline initialization failed: " << pipeline.last_error() << '\n';
        return 1;
    }

    swave::core::SyntheticSource source(
        swave::core::SyntheticSourceConfig{{arguments.width, arguments.height}, arguments.fps});
    std::size_t received{};
    for (std::size_t index = 0; index < arguments.frames; ++index) {
        if (!pipeline.submit(source.next()) || !pipeline.process_one()) {
            std::cerr << "pipeline processing failed: " << pipeline.last_error() << '\n';
            return 1;
        }
        swave::core::FramePacket output;
        while (pipeline.receive(output)) {
            ++received;
        }
    }
    pipeline.stop();

    const auto& stats = pipeline.stats();
    std::cout << "sWAVe pipeline smoke\n"
              << "input=" << stats.processed_input << '\n'
              << "output=" << received << '\n'
              << "dropped_input=" << stats.dropped_input << '\n'
              << "dropped_output=" << stats.dropped_output << '\n'
              << "backend_errors=" << stats.backend_errors << '\n';
    return stats.backend_errors == 0 && received != 0 ? 0 : 1;
}
