#include "core/source/synthetic_source.hpp"

#include <chrono>
#include <memory>
#include <vector>

namespace swave::core {

SyntheticSource::SyntheticSource(SyntheticSourceConfig config)
    : config_(config) {
    if (config_.fps == 0) {
        config_.fps = 30;
    }
}

FramePacket SyntheticSource::next() {
    const auto pixel_count = static_cast<std::size_t>(config_.resolution.width) * config_.resolution.height;
    auto bytes = std::make_shared<std::vector<std::byte>>(pixel_count * 4);
    const auto phase = static_cast<std::uint8_t>(sequence_ % 255);
    for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
        const auto offset = pixel * 4;
        (*bytes)[offset] = static_cast<std::byte>(phase);
        (*bytes)[offset + 1] = static_cast<std::byte>((pixel + phase) % 255);
        (*bytes)[offset + 2] = static_cast<std::byte>((sequence_ * 3) % 255);
        (*bytes)[offset + 3] = std::byte{0xff};
    }

    const auto frame_duration = std::chrono::microseconds{1'000'000 / config_.fps};
    FramePacket frame;
    frame.surface.format = PixelFormat::bgra8;
    frame.surface.width = config_.resolution.width;
    frame.surface.height = config_.resolution.height;
    frame.surface.bytes = std::move(bytes);
    frame.media_pts = frame_duration * sequence_;
    frame.capture_time = std::chrono::steady_clock::now();
    frame.duration = frame_duration;
    frame.sequence = sequence_++;
    return frame;
}

} // namespace swave::core
