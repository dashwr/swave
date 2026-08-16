#include "core/backend/video_backends.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace swave::core {
namespace {

bool initialize_session(
    ModelKind expected_kind,
    const ModelManifest& manifest,
    const std::shared_ptr<IInferenceSession>& session,
    const InferenceOptions& options,
    std::string& error) {
    if (manifest.kind != expected_kind) {
        error = "model kind does not match backend";
        return false;
    }
    if (!session) {
        error = "inference session is null";
        return false;
    }
    if (manifest.input_layout != TensorLayout::nchw) {
        error = "alpha backend supports NCHW manifests only";
        return false;
    }
    if (manifest.input_names.empty() && expected_kind == ModelKind::interpolator) {
        error = "interpolator manifest requires input_names";
        return false;
    }
    return session->load(manifest, options, error);
}

bool frame_to_tensor(const FramePacket& frame, Tensor& tensor, std::string& error) {
    if (!frame.surface.bytes || frame.surface.width == 0 || frame.surface.height == 0) {
        error = "frame has no pixel data";
        return false;
    }
    if (frame.surface.format != PixelFormat::bgra8 && frame.surface.format != PixelFormat::rgba8) {
        error = "onnx video backend requires BGRA8 or RGBA8 input";
        return false;
    }
    const auto pixel_count = static_cast<std::size_t>(frame.surface.width) * frame.surface.height;
    if (frame.surface.bytes->size() < pixel_count * 4) {
        error = "frame pixel buffer is smaller than its dimensions";
        return false;
    }
    tensor.shape = {1, 3, frame.surface.height, frame.surface.width};
    tensor.values.resize(pixel_count * 3);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const auto offset = pixel * 4;
        const auto red = frame.surface.format == PixelFormat::bgra8 ? offset + 2 : offset;
        const auto green = offset + 1;
        const auto blue = frame.surface.format == PixelFormat::bgra8 ? offset : offset + 2;
        tensor.values[pixel] = static_cast<float>((*frame.surface.bytes)[red]) / 255.0F;
        tensor.values[pixel_count + pixel] = static_cast<float>((*frame.surface.bytes)[green]) / 255.0F;
        tensor.values[2 * pixel_count + pixel] = static_cast<float>((*frame.surface.bytes)[blue]) / 255.0F;
    }
    return true;
}

bool tensor_to_frame(const Tensor& tensor, const FramePacket& source, FramePacket& output, std::string& error) {
    if (tensor.shape.size() != 4 || tensor.shape[0] != 1 || tensor.shape[1] != 3 ||
        tensor.shape[2] <= 0 || tensor.shape[3] <= 0) {
        error = "onnx video output must have NCHW shape [1,3,H,W]";
        return false;
    }
    const auto height = static_cast<std::uint32_t>(tensor.shape[2]);
    const auto width = static_cast<std::uint32_t>(tensor.shape[3]);
    const auto pixel_count = static_cast<std::size_t>(width) * height;
    if (pixel_count > static_cast<std::size_t>(-1) / 3 ||
        tensor.values.size() != pixel_count * 3) {
        error = "onnx output tensor size does not match its shape";
        return false;
    }
    if (!source.surface.bytes || source.surface.width == 0 || source.surface.height == 0 ||
        source.surface.bytes->size() < static_cast<std::size_t>(source.surface.width) * source.surface.height * 4) {
        error = "source frame has no valid alpha buffer";
        return false;
    }
    auto bytes = std::make_shared<std::vector<std::byte>>(pixel_count * 4);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const auto to_byte = [](float value) {
            return static_cast<std::byte>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
        };
        const auto offset = pixel * 4;
        (*bytes)[offset] = to_byte(tensor.values[2 * pixel_count + pixel]);
        (*bytes)[offset + 1] = to_byte(tensor.values[pixel_count + pixel]);
        (*bytes)[offset + 2] = to_byte(tensor.values[pixel]);
        const auto output_x = pixel % width;
        const auto output_y = pixel / width;
        const auto source_x = std::min<std::uint32_t>(
            source.surface.width - 1,
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(output_x) * source.surface.width / width));
        const auto source_y = std::min<std::uint32_t>(
            source.surface.height - 1,
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(output_y) * source.surface.height / height));
        const auto source_offset = (static_cast<std::size_t>(source_y) * source.surface.width + source_x) * 4;
        (*bytes)[offset + 3] = (*source.surface.bytes)[source_offset + 3];
    }
    output = source;
    output.surface.format = PixelFormat::bgra8;
    output.surface.width = width;
    output.surface.height = height;
    output.surface.bytes = std::move(bytes);
    return true;
}

} // namespace

bool FakeUpscalerBackend::initialize(
    const ModelManifest& manifest,
    std::shared_ptr<IInferenceSession> session,
    const InferenceOptions& options,
    std::string& error) {
    if (!initialize_session(ModelKind::upscaler, manifest, session, options, error)) {
        return false;
    }
    session_ = std::move(session);
    manifest_ = manifest;
    initialized_ = true;
    return true;
}

bool FakeUpscalerBackend::process(
    const FramePacket& input,
    FramePacket& output,
    std::string& error) {
    if (!initialized_) {
        error = "upscaler backend is not initialized";
        return false;
    }
    std::vector<Tensor> outputs;
    if (!session_->run({}, outputs, error)) {
        return false;
    }
    output = input;
    return true;
}

void FakeUpscalerBackend::flush() noexcept {}

void FakeUpscalerBackend::close() noexcept {
    if (session_) {
        session_->close();
    }
    session_.reset();
    initialized_ = false;
}

bool FakeInterpolatorBackend::initialize(
    const ModelManifest& manifest,
    std::shared_ptr<IInferenceSession> session,
    const InferenceOptions& options,
    std::string& error) {
    if (!initialize_session(ModelKind::interpolator, manifest, session, options, error)) {
        return false;
    }
    session_ = std::move(session);
    manifest_ = manifest;
    initialized_ = true;
    return true;
}

bool FakeInterpolatorBackend::process(
    const FramePacket& first,
    const FramePacket& second,
    double timestep,
    FramePacket& output,
    std::string& error) {
    if (!initialized_) {
        error = "interpolator backend is not initialized";
        return false;
    }
    if (timestep < 0.0 || timestep > 1.0) {
        error = "interpolation timestep must be between 0 and 1";
        return false;
    }
    std::vector<Tensor> outputs;
    if (!session_->run({}, outputs, error)) {
        return false;
    }
    output = timestep < 0.5 ? first : second;
    output.media_pts = first.media_pts +
        std::chrono::duration_cast<std::chrono::microseconds>(
            (second.media_pts - first.media_pts) * timestep);
    output.discontinuity = first.discontinuity || second.discontinuity;
    return true;
}

void FakeInterpolatorBackend::flush() noexcept {}

void FakeInterpolatorBackend::close() noexcept {
    if (session_) {
        session_->close();
    }
    session_.reset();
    initialized_ = false;
}

bool OnnxUpscalerBackend::initialize(
    const ModelManifest& manifest,
    std::shared_ptr<IInferenceSession> session,
    const InferenceOptions& options,
    std::string& error) {
    if (!initialize_session(ModelKind::upscaler, manifest, session, options, error)) {
        return false;
    }
    session_ = std::move(session);
    manifest_ = manifest;
    initialized_ = true;
    return true;
}

bool OnnxUpscalerBackend::process(
    const FramePacket& input,
    FramePacket& output,
    std::string& error) {
    if (!initialized_) {
        error = "onnx upscaler backend is not initialized";
        return false;
    }
    Tensor input_tensor;
    if (!frame_to_tensor(input, input_tensor, error)) {
        return false;
    }
    std::vector<Tensor> outputs;
    if (!session_->run({input_tensor}, outputs, error) || outputs.empty()) {
        if (error.empty()) error = "onnx upscaler returned no output";
        return false;
    }
    const auto& tensor = outputs.front();
    const auto expected_width = static_cast<std::int64_t>(std::lround(
        static_cast<double>(input.surface.width) * manifest_.native_scale));
    const auto expected_height = static_cast<std::int64_t>(std::lround(
        static_cast<double>(input.surface.height) * manifest_.native_scale));
    if (tensor.shape.size() != 4 || tensor.shape[2] != expected_height || tensor.shape[3] != expected_width) {
        error = "upscaler output shape does not match manifest native_scale";
        return false;
    }
    return tensor_to_frame(tensor, input, output, error);
}

void OnnxUpscalerBackend::flush() noexcept {}

void OnnxUpscalerBackend::close() noexcept {
    if (session_) session_->close();
    session_.reset();
    initialized_ = false;
}

bool OnnxInterpolatorBackend::initialize(
    const ModelManifest& manifest,
    std::shared_ptr<IInferenceSession> session,
    const InferenceOptions& options,
    std::string& error) {
    if (!initialize_session(ModelKind::interpolator, manifest, session, options, error)) {
        return false;
    }
    session_ = std::move(session);
    manifest_ = manifest;
    initialized_ = true;
    return true;
}

bool OnnxInterpolatorBackend::process(
    const FramePacket& first,
    const FramePacket& second,
    double timestep,
    FramePacket& output,
    std::string& error) {
    if (!initialized_) {
        error = "onnx interpolator backend is not initialized";
        return false;
    }
    if (timestep < 0.0 || timestep > 1.0) {
        error = "interpolation timestep must be between 0 and 1";
        return false;
    }
    Tensor first_tensor;
    Tensor second_tensor;
    if (!frame_to_tensor(first, first_tensor, error) || !frame_to_tensor(second, second_tensor, error)) {
        return false;
    }
    if (first_tensor.shape != second_tensor.shape) {
        error = "interpolator frame shapes do not match";
        return false;
    }
    Tensor timestep_tensor{{1}, {static_cast<float>(timestep)}};
    std::vector<Tensor> outputs;
    if (!session_->run({first_tensor, second_tensor, timestep_tensor}, outputs, error) || outputs.empty()) {
        if (error.empty()) error = "onnx interpolator returned no output";
        return false;
    }
    if (!tensor_to_frame(outputs.front(), first, output, error)) {
        return false;
    }
    const auto first_alpha_size = static_cast<std::size_t>(first.surface.width) * first.surface.height * 4;
    const auto second_alpha_size = static_cast<std::size_t>(second.surface.width) * second.surface.height * 4;
    if (!first.surface.bytes || !second.surface.bytes ||
        first.surface.bytes->size() < first_alpha_size || second.surface.bytes->size() < second_alpha_size) {
        error = "interpolator source frames have no valid alpha buffers";
        return false;
    }
    for (std::uint32_t y = 0; y < output.surface.height; ++y) {
        for (std::uint32_t x = 0; x < output.surface.width; ++x) {
            const auto first_x = std::min<std::uint32_t>(
                first.surface.width - 1,
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * first.surface.width / output.surface.width));
            const auto first_y = std::min<std::uint32_t>(
                first.surface.height - 1,
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) * first.surface.height / output.surface.height));
            const auto second_x = std::min<std::uint32_t>(
                second.surface.width - 1,
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * second.surface.width / output.surface.width));
            const auto second_y = std::min<std::uint32_t>(
                second.surface.height - 1,
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) * second.surface.height / output.surface.height));
            const auto first_offset = (static_cast<std::size_t>(first_y) * first.surface.width + first_x) * 4 + 3;
            const auto second_offset = (static_cast<std::size_t>(second_y) * second.surface.width + second_x) * 4 + 3;
            const auto first_alpha = static_cast<unsigned>(std::to_integer<unsigned char>((*first.surface.bytes)[first_offset]));
            const auto second_alpha = static_cast<unsigned>(std::to_integer<unsigned char>((*second.surface.bytes)[second_offset]));
            const auto alpha = static_cast<unsigned>(std::lround(
                static_cast<double>(first_alpha) * (1.0 - timestep) + static_cast<double>(second_alpha) * timestep));
            (*output.surface.bytes)[(static_cast<std::size_t>(y) * output.surface.width + x) * 4 + 3] =
                static_cast<std::byte>(alpha);
        }
    }
    output.media_pts = first.media_pts +
        std::chrono::duration_cast<std::chrono::microseconds>(
            (second.media_pts - first.media_pts) * timestep);
    output.discontinuity = first.discontinuity || second.discontinuity;
    return true;
}

void OnnxInterpolatorBackend::flush() noexcept {}

void OnnxInterpolatorBackend::close() noexcept {
    if (session_) session_->close();
    session_.reset();
    initialized_ = false;
}

} // namespace swave::core
