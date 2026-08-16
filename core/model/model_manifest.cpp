#include "core/model/model_manifest.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>

namespace swave::core {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parse_double(std::string_view value, double& output) {
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parse_bool(std::string_view value, bool& output) {
    if (value == "true" || value == "1") {
        output = true;
        return true;
    }
    if (value == "false" || value == "0") {
        output = false;
        return true;
    }
    return false;
}

} // namespace

bool ModelManifest::validate(std::string& error) const {
    if (id.empty()) {
        error = "model id is empty";
        return false;
    }
    if (model_path.empty()) {
        error = "model path is empty";
        return false;
    }
    if (!std::isfinite(native_scale) || !std::isfinite(min_scale) || !std::isfinite(max_scale) ||
        native_scale <= 0.0 || min_scale <= 0.0 || max_scale < min_scale ||
        native_scale < min_scale || native_scale > max_scale) {
        error = "invalid scale range";
        return false;
    }
    if (kind == ModelKind::interpolator && !arbitrary_timestep && native_scale != 1.0) {
        error = "interpolator scale must be 1 unless it is timestep-based";
        return false;
    }
    return true;
}

std::optional<ModelManifest> ModelManifest::from_text(
    std::string_view text,
    std::string& error) {
    ModelManifest manifest;
    std::istringstream input{std::string{text}};
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            error = "manifest line " + std::to_string(line_number) + " has no '='";
            return std::nullopt;
        }
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));

        if (key == "native_scale") {
            if (!parse_double(value, manifest.native_scale)) {
                error = "invalid native_scale"; return std::nullopt;
            }
            continue;
        }
        if (key == "min_scale") {
            if (!parse_double(value, manifest.min_scale)) {
                error = "invalid min_scale"; return std::nullopt;
            }
            continue;
        }
        if (key == "max_scale") {
            if (!parse_double(value, manifest.max_scale)) {
                error = "invalid max_scale"; return std::nullopt;
            }
            continue;
        }
        if (key == "arbitrary_timestep") {
            if (!parse_bool(value, manifest.arbitrary_timestep)) {
                error = "invalid arbitrary_timestep"; return std::nullopt;
            }
            continue;
        }

        if (key == "id") manifest.id = value;
        else if (key == "kind") {
            if (value == "upscaler") manifest.kind = ModelKind::upscaler;
            else if (value == "interpolator") manifest.kind = ModelKind::interpolator;
            else { error = "unknown model kind"; return std::nullopt; }
        } else if (key == "precision") {
            if (value == "fp32") manifest.precision = ModelPrecision::fp32;
            else if (value == "fp16") manifest.precision = ModelPrecision::fp16;
            else if (value == "int8") manifest.precision = ModelPrecision::int8;
            else { error = "unknown model precision"; return std::nullopt; }
        } else if (key == "input_layout") {
            if (value == "nchw") manifest.input_layout = TensorLayout::nchw;
            else if (value == "nhwc") manifest.input_layout = TensorLayout::nhwc;
            else { error = "unknown tensor layout"; return std::nullopt; }
        } else if (key == "model_path") manifest.model_path = value;
        else if (key == "engine_path") manifest.engine_path = value;
        else if (key == "input_names") manifest.input_names = value;
        else if (key == "output_names") manifest.output_names = value;
        else if (key == "license") manifest.license = value;
        else if (key == "fixed_shape") {
            const auto x = value.find('x');
            if (x == std::string::npos) { error = "invalid fixed_shape"; return std::nullopt; }
            std::uint32_t width{};
            std::uint32_t height{};
            const auto width_text = value.substr(0, x);
            const auto height_text = value.substr(x + 1);
            const auto width_result = std::from_chars(width_text.data(), width_text.data() + width_text.size(), width);
            const auto height_result = std::from_chars(height_text.data(), height_text.data() + height_text.size(), height);
            if (width_result.ec != std::errc{} || height_result.ec != std::errc{} ||
                width_result.ptr != width_text.data() + width_text.size() ||
                height_result.ptr != height_text.data() + height_text.size() ||
                width == 0 || height == 0) {
                error = "invalid fixed_shape"; return std::nullopt;
            }
            manifest.fixed_shapes.push_back({{width, height}, 3});
        } else {
            error = "unknown manifest key: " + key;
            return std::nullopt;
        }
    }

    if (!manifest.validate(error)) {
        return std::nullopt;
    }
    return manifest;
}

std::optional<ModelManifest> ModelManifest::load_file(
    const std::filesystem::path& path,
    std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "cannot open manifest: " + path.string();
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    auto manifest = from_text(contents.str(), error);
    if (!manifest) {
        return std::nullopt;
    }
    const auto base = path.parent_path();
    if (manifest->model_path.is_relative()) {
        manifest->model_path = base / manifest->model_path;
    }
    if (!manifest->engine_path.empty() && manifest->engine_path.is_relative()) {
        manifest->engine_path = base / manifest->engine_path;
    }
    return manifest;
}

bool ModelCatalog::add(ModelManifest manifest, std::string& error) {
    if (!manifest.validate(error)) {
        return false;
    }
    if (find(manifest.id) != nullptr) {
        error = "duplicate model id: " + manifest.id;
        return false;
    }
    models_.push_back(std::move(manifest));
    return true;
}

bool ModelCatalog::load_directory(
    const std::filesystem::path& directory,
    std::string& error) {
    if (!std::filesystem::is_directory(directory)) {
        error = "model directory does not exist: " + directory.string();
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".swave-model") {
            continue;
        }
        auto manifest = ModelManifest::load_file(entry.path(), error);
        if (!manifest || !add(std::move(*manifest), error)) {
            return false;
        }
    }
    return true;
}

const ModelManifest* ModelCatalog::find(std::string_view id) const noexcept {
    const auto result = std::find_if(models_.begin(), models_.end(), [id](const ModelManifest& model) {
        return model.id == id;
    });
    return result == models_.end() ? nullptr : &*result;
}

} // namespace swave::core
