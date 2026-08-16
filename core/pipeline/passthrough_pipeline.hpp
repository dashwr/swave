#pragma once

#include "core/frame/frame_packet.hpp"
#include "core/pipeline/pipeline_state.hpp"
#include "core/queue/bounded_queue.hpp"

#include <cstddef>
#include <cstdint>
#include <chrono>

namespace swave::core {

struct PipelineConfig {
    std::size_t input_capacity{60};
    std::size_t output_capacity{60};
    std::chrono::microseconds target_delay{std::chrono::seconds(1)};
};

struct PipelineStats {
    std::uint64_t submitted{};
    std::uint64_t dropped_input{};
    std::uint64_t processed{};
    std::uint64_t dropped_output{};
};

class PassthroughPipeline {
public:
    explicit PassthroughPipeline(PipelineConfig config);

    void start();
    void stop();

    [[nodiscard]] bool submit(FramePacket frame);
    [[nodiscard]] bool process_one();
    [[nodiscard]] bool receive(FramePacket& frame);

    [[nodiscard]] PipelineState state() const noexcept { return state_; }
    [[nodiscard]] const PipelineStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const PipelineConfig& config() const noexcept { return config_; }

private:
    PipelineConfig config_;
    BoundedQueue<FramePacket> input_;
    BoundedQueue<FramePacket> output_;
    PipelineState state_{PipelineState::disconnected};
    PipelineStats stats_;
};

} // namespace swave::core
