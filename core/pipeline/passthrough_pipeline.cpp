#include "core/pipeline/passthrough_pipeline.hpp"

#include <utility>

namespace swave::core {

PassthroughPipeline::PassthroughPipeline(PipelineConfig config)
    : config_(std::move(config)),
      input_(config_.input_capacity),
      output_(config_.output_capacity) {}

void PassthroughPipeline::start() {
    state_ = PipelineState::buffering;
}

void PassthroughPipeline::stop() {
    state_ = PipelineState::stopping;
    input_.close();
    output_.close();
    state_ = PipelineState::disconnected;
}

bool PassthroughPipeline::submit(FramePacket frame) {
    if (state_ == PipelineState::disconnected || input_.closed()) {
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

bool PassthroughPipeline::process_one() {
    FramePacket frame;
    if (!input_.try_pop(frame)) {
        return false;
    }
    if (!output_.try_push(std::move(frame))) {
        ++stats_.dropped_output;
        state_ = PipelineState::degraded;
        return false;
    }
    ++stats_.processed;
    state_ = PipelineState::running;
    return true;
}

bool PassthroughPipeline::receive(FramePacket& frame) {
    return output_.try_pop(frame);
}

} // namespace swave::core
