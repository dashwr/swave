#pragma once

#include "core/frame/frame_packet.hpp"
#include "core/frame/resolution.hpp"

#include <cstddef>
#include <cstdint>

namespace swave::core {

struct SyntheticSourceConfig {
    Resolution resolution{640, 360};
    unsigned fps{30};
};

class SyntheticSource final {
public:
    explicit SyntheticSource(SyntheticSourceConfig config);

    [[nodiscard]] FramePacket next();
    [[nodiscard]] std::uint64_t produced() const noexcept { return sequence_; }

private:
    SyntheticSourceConfig config_;
    std::uint64_t sequence_{};
};

} // namespace swave::core
