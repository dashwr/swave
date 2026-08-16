#pragma once

#include "core/frame/resolution.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace swave::core {

struct ProcessingPreset {
    std::string id;
    double scale_factor{1.0};
    unsigned output_fps{};
    bool interpolation_enabled{true};
};

[[nodiscard]] inline std::vector<ProcessingPreset> default_presets(
    unsigned source_fps) {
    const auto output_fps = source_fps == 0 ? 60U : source_fps;
    const std::array factors{1.0, 1.5, 2.0, 3.0, 4.0, 5.0};
    std::vector<ProcessingPreset> presets;
    presets.reserve(factors.size());
    for (const auto factor : factors) {
        presets.push_back({"scale-" + std::to_string(factor), factor, output_fps, true});
    }
    return presets;
}

[[nodiscard]] inline bool valid_custom_factor(double factor) noexcept {
    return std::isfinite(factor) && factor >= 1.1 && factor <= 5.0;
}

[[nodiscard]] inline Resolution preset_resolution(
    Resolution source,
    Resolution monitor,
    const ProcessingPreset& preset) noexcept {
    return output_resolution(source, std::max(1.0, preset.scale_factor), monitor);
}

} // namespace swave::core
