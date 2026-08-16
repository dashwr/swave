#include "core/frame/frame_packet.hpp"
#include "core/frame/resolution.hpp"
#include "core/backend/video_backends.hpp"
#include "core/inference/inference_session.hpp"
#include "core/model/model_manifest.hpp"
#include "core/pipeline/passthrough_pipeline.hpp"
#include "core/pipeline/processing_pipeline.hpp"
#include "core/processing/presets.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

using namespace swave::core;

int main() {
    const auto base = output_resolution({1920, 1080}, 1.0, {2560, 1440});
    assert(base.width == 1920 && base.height == 1080);

    const auto limited = output_resolution({1920, 1080}, 2.0, {2560, 1440});
    assert(limited.width == 2560 && limited.height == 1440);

    const auto presets = default_presets(30);
    assert(presets.size() == 6);
    assert(presets.front().scale_factor == 1.0);
    assert(presets.back().scale_factor == 5.0);
    assert(preset_resolution({1920, 1080}, {2560, 1440}, presets[2]).width == 2560);
    assert(valid_custom_factor(1.1));
    assert(!valid_custom_factor(1.0));

    constexpr auto manifest_text =
        "id=srvgv-test\n"
        "kind=upscaler\n"
        "precision=fp16\n"
        "model_path=models/srvgv.onnx\n"
        "native_scale=4\n"
        "min_scale=1.1\n"
        "max_scale=5\n"
        "fixed_shape=1920x1080\n";
    std::string error;
    const auto manifest = ModelManifest::from_text(manifest_text, error);
    assert(manifest.has_value());
    ModelCatalog catalog;
    assert(catalog.add(*manifest, error));
    assert(catalog.find("srvgv-test") != nullptr);
    auto invalid_manifest = ModelManifest::from_text(
        "id=invalid\nkind=upscaler\nmodel_path=model.onnx\nnative_scale=4\nmin_scale=1.1\nmax_scale=5\nfixed_shape=1920x1080x1\n",
        error);
    assert(!invalid_manifest.has_value());

    FakeInferenceSession session;
    assert(session.load(*manifest, {}, error));
    std::vector<Tensor> outputs;
    assert(session.run({{{1, 3, 2, 2}, {1.0F}}}, outputs, error));
    assert(outputs.size() == 1);
    session.close();

    PipelineConfig config;
    config.input_capacity = 2;
    config.output_capacity = 2;
    PassthroughPipeline pipeline(config);
    pipeline.start();

    FramePacket frame;
    frame.surface.format = PixelFormat::rgba8;
    frame.surface.width = 1920;
    frame.surface.height = 1080;
    frame.surface.bytes = std::make_shared<std::vector<std::byte>>(
        static_cast<std::size_t>(frame.surface.width) * frame.surface.height * 4);
    frame.media_pts = std::chrono::milliseconds(33);

    auto upscaler_session = std::make_shared<FakeInferenceSession>();
    FakeUpscalerBackend upscaler;
    assert(upscaler.initialize(*manifest, upscaler_session, {}, error));
    FramePacket upscaled;
    assert(upscaler.process(frame, upscaled, error));
    assert(upscaled.media_pts == frame.media_pts);
    upscaler.close();

    ModelManifest interpolator_manifest = *manifest;
    interpolator_manifest.id = "rife-test";
    interpolator_manifest.kind = ModelKind::interpolator;
    interpolator_manifest.arbitrary_timestep = true;
    auto interpolator_session = std::make_shared<FakeInferenceSession>();
    FakeInterpolatorBackend interpolator;
    assert(interpolator.initialize(interpolator_manifest, interpolator_session, {}, error));
    FramePacket second = frame;
    second.media_pts = std::chrono::milliseconds(66);
    FramePacket interpolated;
    assert(interpolator.process(frame, second, 0.5, interpolated, error));
    assert(interpolated.media_pts == std::chrono::microseconds(49'500));
    interpolator.close();

    auto tensor_upscaler = std::make_shared<OnnxUpscalerBackend>();
    assert(tensor_upscaler->initialize(*manifest, std::make_shared<FakeInferenceSession>(), {}, error));
    FramePacket tensor_upscaled;
    assert(tensor_upscaler->process(frame, tensor_upscaled, error));
    assert(tensor_upscaled.surface.width == frame.surface.width);
    assert(tensor_upscaled.surface.height == frame.surface.height);
    tensor_upscaler->close();

    auto tensor_interpolator = std::make_shared<OnnxInterpolatorBackend>();
    assert(tensor_interpolator->initialize(
        interpolator_manifest, std::make_shared<FakeInferenceSession>(), {}, error));
    FramePacket tensor_interpolated;
    assert(tensor_interpolator->process(frame, second, 0.5, tensor_interpolated, error));
    assert(tensor_interpolated.surface.format == PixelFormat::bgra8);
    tensor_interpolator->close();

    auto pipeline_upscaler = std::make_shared<FakeUpscalerBackend>();
    auto pipeline_interpolator = std::make_shared<FakeInterpolatorBackend>();
    assert(pipeline_upscaler->initialize(*manifest, std::make_shared<FakeInferenceSession>(), {}, error));
    assert(pipeline_interpolator->initialize(interpolator_manifest, std::make_shared<FakeInferenceSession>(), {}, error));
    ProcessingPipeline processing_pipeline({4, 8, true}, pipeline_upscaler, pipeline_interpolator);
    processing_pipeline.start();
    assert(processing_pipeline.state() == PipelineState::buffering);
    assert(processing_pipeline.submit(frame));
    assert(processing_pipeline.process_one());
    assert(processing_pipeline.submit(second));
    assert(processing_pipeline.process_one());
    assert(processing_pipeline.stats().produced_output == 3);
    FramePacket processed;
    assert(processing_pipeline.receive(processed));
    assert(processing_pipeline.receive(processed));
    assert(processing_pipeline.receive(processed));
    processing_pipeline.stop();
    assert(processing_pipeline.state() == PipelineState::disconnected);

    assert(pipeline.submit(frame));
    assert(pipeline.process_one());

    FramePacket received;
    assert(pipeline.receive(received));
    assert(received.surface.width == frame.surface.width);
    assert(pipeline.stats().processed == 1);

    pipeline.stop();
    assert(pipeline.state() == PipelineState::disconnected);
}
