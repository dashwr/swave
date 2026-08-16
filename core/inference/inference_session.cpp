#include "core/inference/inference_session.hpp"

#ifdef SWAVE_HAS_ONNXRUNTIME
#include "core/inference/onnxruntime_session.hpp"
#endif

#include <memory>

namespace swave::core {

bool FakeInferenceSession::load(
    const ModelManifest& manifest,
    const InferenceOptions&,
    std::string& error) {
    if (!manifest.validate(error)) {
        return false;
    }
    loaded_ = true;
    return true;
}

bool FakeInferenceSession::run(
    const std::vector<Tensor>& inputs,
    std::vector<Tensor>& outputs,
    std::string& error) {
    if (!loaded_) {
        error = "fake inference session is not loaded";
        return false;
    }
    outputs = inputs;
    return true;
}

void FakeInferenceSession::close() noexcept {
    loaded_ = false;
}

std::shared_ptr<IInferenceSession> make_inference_session(
    const InferenceOptions& options,
    std::string& error) {
    if (options.provider == InferenceProvider::fake) {
        return std::make_shared<FakeInferenceSession>();
    }
#ifdef SWAVE_HAS_ONNXRUNTIME
    return std::make_shared<OnnxRuntimeSession>();
#else
    error = "requested inference provider is not compiled in this build";
    return {};
#endif
}

} // namespace swave::core
