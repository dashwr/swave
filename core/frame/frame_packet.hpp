#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace swave::core {

enum class PixelFormat {
    unknown,
    rgba8,
    bgra8,
    nv12,
};

struct FrameSurface {
    PixelFormat format{PixelFormat::unknown};
    std::uint32_t width{};
    std::uint32_t height{};
    std::shared_ptr<std::vector<std::byte>> bytes;
};

struct FramePacket {
    FrameSurface surface;
    std::chrono::microseconds media_pts{};
    std::chrono::steady_clock::time_point capture_time{};
    std::chrono::microseconds duration{};
    std::uint64_t sequence{};
    bool discontinuity{};
};

} // namespace swave::core
