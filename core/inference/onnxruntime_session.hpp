#pragma once

#include "core/inference/inference_session.hpp"

namespace swave::core {

class OnnxRuntimeSession final : public IInferenceSession {
public:
    OnnxRuntimeSession();
    ~OnnxRuntimeSession() override;

    [[nodiscard]] bool load(
        const ModelManifest& manifest,
        const InferenceOptions& options,
        std::string& error) override;
    [[nodiscard]] bool run(
        const std::vector<Tensor>& inputs,
        std::vector<Tensor>& outputs,
        std::string& error) override;
    void close() noexcept override;
    [[nodiscard]] InferenceProvider provider() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace swave::core
