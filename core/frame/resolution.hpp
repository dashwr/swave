#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace swave::core {

struct Resolution {
    std::uint32_t width{};
    std::uint32_t height{};

    [[nodiscard]] bool valid() const noexcept {
        return width != 0 && height != 0;
    }
};

[[nodiscard]] inline Resolution output_resolution(
    Resolution source,
    double factor,
    Resolution monitor) noexcept {
    if (!source.valid() || !std::isfinite(factor) || factor <= 0.0) {
        return {};
    }

    const auto requested_width = static_cast<double>(source.width) * factor;
    const auto requested_height = static_cast<double>(source.height) * factor;
    double monitor_scale = 1.0;

    if (monitor.valid()) {
        monitor_scale = std::min(
            1.0,
            std::min(
                static_cast<double>(monitor.width) / requested_width,
                static_cast<double>(monitor.height) / requested_height));
    }

    return {
        static_cast<std::uint32_t>(std::max<long>(1, std::lround(requested_width * monitor_scale))),
        static_cast<std::uint32_t>(std::max<long>(1, std::lround(requested_height * monitor_scale))),
    };
}

} // namespace swave::core
