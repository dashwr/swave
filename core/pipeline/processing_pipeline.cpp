#include "core/pipeline/processing_pipeline.hpp"

#include <utility>

namespace swave::core {

ProcessingPipeline::ProcessingPipeline(
    ProcessingPipelineConfig config,
    std::shared_ptr<IUpscalerBackend> upscaler,
    std::shared_ptr<IInterpolatorBackend> interpolator)
    : config_(std::move(config)),
      upscaler_(std::move(upscaler)),
      interpolator_(std::move(interpolator)),
      input_(config_.input_capacity),
      output_(config_.output_capacity) {}

void ProcessingPipeline::start() {
    if (!upscaler_ || (config_.interpolation_enabled && !interpolator_)) {
        last_error_ = "processing backends are not configured";
        state_ = PipelineState::error;
        return;
    }
    state_ = PipelineState::buffering;
}

void ProcessingPipeline::stop() {
    state_ = PipelineState::stopping;
    input_.close();
    output_.close();
    previous_.reset();
    if (upscaler_) {
        upscaler_->flush();
        upscaler_->close();
    }
    if (interpolator_) {
        interpolator_->flush();
        interpolator_->close();
    }
    state_ = PipelineState::disconnected;
}

bool ProcessingPipeline::submit(FramePacket frame) {
    if (state_ == PipelineState::disconnected || state_ == PipelineState::error || input_.closed()) {
        return false;
    }
    if (!input_.try_push(std::move(frame))) {
        ++stats_.dropped_input;
        state_ = PipelineState::degraded;
        return false;
    }
    ++stats_.submitted;
    return true;
}

bool ProcessingPipeline::process_one() {
    if (state_ == PipelineState::disconnected || state_ == PipelineState::error) {
        return false;
    }

    FramePacket current;
    if (!input_.try_pop(current)) {
        return false;
    }
    ++stats_.processed_input;

    if (!previous_) {
        FramePacket first_output;
        if (!upscale(current, first_output) || !publish(first_output)) {
            return false;
        }
        previous_ = std::move(first_output);
        state_ = PipelineState::running;
        return true;
    }

    FramePacket current_output;
    if (!upscale(current, current_output)) {
        return false;
    }
    if (config_.interpolation_enabled && !publish_interpolated(*previous_, current_output)) {
        return false;
    }
    if (!publish(current_output)) {
        return false;
    }
    previous_ = std::move(current_output);
    state_ = PipelineState::running;
    return true;
}

bool ProcessingPipeline::receive(FramePacket& frame) {
    return output_.try_pop(frame);
}

bool ProcessingPipeline::publish(FramePacket frame) {
    if (!output_.try_push(std::move(frame))) {
        ++stats_.dropped_output;
        state_ = PipelineState::degraded;
        return false;
    }
    ++stats_.produced_output;
    return true;
}

bool ProcessingPipeline::upscale(const FramePacket& input, FramePacket& output) {
    if (upscaler_->process(input, output, last_error_)) {
        return true;
    }
    ++stats_.backend_errors;
    state_ = PipelineState::error;
    return false;
}

bool ProcessingPipeline::publish_interpolated(
    const FramePacket& first,
    const FramePacket& second) {
    FramePacket interpolated;
    if (!interpolator_->process(first, second, 0.5, interpolated, last_error_)) {
        ++stats_.backend_errors;
        state_ = PipelineState::error;
        return false;
    }
    return publish(std::move(interpolated));
}

} // namespace swave::core
