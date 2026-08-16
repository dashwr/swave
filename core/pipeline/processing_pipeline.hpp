#pragma once

#include "core/backend/video_backends.hpp"
#include "core/frame/frame_packet.hpp"
#include "core/pipeline/pipeline_state.hpp"
#include "core/queue/bounded_queue.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace swave::core {

struct ProcessingPipelineConfig {
    std::size_t input_capacity{60};
    std::size_t output_capacity{120};
    bool interpolation_enabled{true};
};

struct ProcessingPipelineStats {
    std::uint64_t submitted{};
    std::uint64_t dropped_input{};
    std::uint64_t processed_input{};
    std::uint64_t produced_output{};
    std::uint64_t dropped_output{};
    std::uint64_t backend_errors{};
};

class ProcessingPipeline final {
public:
    ProcessingPipeline(
        ProcessingPipelineConfig config,
        std::shared_ptr<IUpscalerBackend> upscaler,
        std::shared_ptr<IInterpolatorBackend> interpolator = {});

    void start();
    void stop();

    [[nodiscard]] bool submit(FramePacket frame);
    [[nodiscard]] bool process_one();
    [[nodiscard]] bool receive(FramePacket& frame);

    [[nodiscard]] PipelineState state() const noexcept { return state_; }
    [[nodiscard]] const ProcessingPipelineStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    [[nodiscard]] bool publish(FramePacket frame);
    [[nodiscard]] bool upscale(const FramePacket& input, FramePacket& output);
    [[nodiscard]] bool publish_interpolated(const FramePacket& first, const FramePacket& second);

    ProcessingPipelineConfig config_;
    std::shared_ptr<IUpscalerBackend> upscaler_;
    std::shared_ptr<IInterpolatorBackend> interpolator_;
    BoundedQueue<FramePacket> input_;
    BoundedQueue<FramePacket> output_;
    std::optional<FramePacket> previous_;
    PipelineState state_{PipelineState::disconnected};
    ProcessingPipelineStats stats_;
    std::string last_error_;
};

} // namespace swave::core
