#pragma once

#include "core/model/model_manifest.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace swave::core {

enum class InferenceProvider {
    fake,
    tensorrt,
    cuda,
};

struct Tensor {
    std::vector<std::int64_t> shape;
    std::vector<float> values;
};

struct InferenceOptions {
    InferenceProvider provider{InferenceProvider::tensorrt};
    std::size_t workspace_bytes{4ULL * 1024ULL * 1024ULL * 1024ULL};
    bool fp16{true};
    bool engine_cache{true};
    std::filesystem::path engine_cache_path{"cache/engines"};
};

class IInferenceSession {
public:
    virtual ~IInferenceSession() = default;

    [[nodiscard]] virtual bool load(
        const ModelManifest& manifest,
        const InferenceOptions& options,
        std::string& error) = 0;
    [[nodiscard]] virtual bool run(
        const std::vector<Tensor>& inputs,
        std::vector<Tensor>& outputs,
        std::string& error) = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual InferenceProvider provider() const noexcept = 0;
};

class FakeInferenceSession final : public IInferenceSession {
public:
    [[nodiscard]] bool load(
        const ModelManifest& manifest,
        const InferenceOptions& options,
        std::string& error) override;
    [[nodiscard]] bool run(
        const std::vector<Tensor>& inputs,
        std::vector<Tensor>& outputs,
        std::string& error) override;
    void close() noexcept override;
    [[nodiscard]] InferenceProvider provider() const noexcept override {
        return InferenceProvider::fake;
    }

private:
    bool loaded_{};
};

[[nodiscard]] std::shared_ptr<IInferenceSession> make_inference_session(
    const InferenceOptions& options,
    std::string& error);

} // namespace swave::core
