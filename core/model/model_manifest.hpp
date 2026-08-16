#pragma once

#include "core/frame/resolution.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace swave::core {

enum class ModelKind {
    upscaler,
    interpolator,
};

enum class ModelPrecision {
    fp32,
    fp16,
    int8,
};

enum class TensorLayout {
    nchw,
    nhwc,
};

struct ModelShape {
    Resolution resolution;
    std::int64_t channels{3};
};

struct ModelManifest {
    std::string id;
    ModelKind kind{ModelKind::upscaler};
    ModelPrecision precision{ModelPrecision::fp16};
    TensorLayout input_layout{TensorLayout::nchw};
    std::filesystem::path model_path;
    std::filesystem::path engine_path;
    std::string input_names;
    std::string output_names;
    std::string license;
    double native_scale{1.0};
    double min_scale{1.0};
    double max_scale{1.0};
    bool arbitrary_timestep{};
    std::vector<ModelShape> fixed_shapes;

    [[nodiscard]] bool validate(std::string& error) const;
    [[nodiscard]] static std::optional<ModelManifest> from_text(
        std::string_view text,
        std::string& error);
    [[nodiscard]] static std::optional<ModelManifest> load_file(
        const std::filesystem::path& path,
        std::string& error);
};

class ModelCatalog {
public:
    [[nodiscard]] bool add(ModelManifest manifest, std::string& error);
    [[nodiscard]] bool load_directory(
        const std::filesystem::path& directory,
        std::string& error);

    [[nodiscard]] const std::vector<ModelManifest>& models() const noexcept {
        return models_;
    }

    [[nodiscard]] const ModelManifest* find(std::string_view id) const noexcept;

private:
    std::vector<ModelManifest> models_;
};

} // namespace swave::core
