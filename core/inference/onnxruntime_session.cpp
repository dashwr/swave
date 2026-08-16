#include "core/inference/onnxruntime_session.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <utility>

namespace swave::core {
namespace {

std::vector<std::string> split_names(const std::string& names) {
    std::vector<std::string> result;
    std::stringstream stream(names);
    std::string name;
    while (std::getline(stream, name, ',')) {
        const auto first = name.find_first_not_of(" \t");
        const auto last = name.find_last_not_of(" \t");
        if (first != std::string::npos) {
            result.push_back(name.substr(first, last - first + 1));
        }
    }
    return result;
}

} // namespace

struct OnnxRuntimeSession::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "swave"};
    std::unique_ptr<Ort::Session> session;
    InferenceProvider provider{InferenceProvider::cuda};
    ModelManifest manifest;
    bool loaded{};
};

OnnxRuntimeSession::OnnxRuntimeSession()
    : impl_(std::make_unique<Impl>()) {}

OnnxRuntimeSession::~OnnxRuntimeSession() = default;

bool OnnxRuntimeSession::load(
    const ModelManifest& manifest,
    const InferenceOptions& options,
    std::string& error) {
    if (!manifest.validate(error)) {
        return false;
    }
    try {
        if (!manifest.model_path.empty() && !std::filesystem::exists(manifest.model_path)) {
            error = "ONNX model does not exist: " + manifest.model_path.string();
            return false;
        }
        if (options.engine_cache && !options.engine_cache_path.empty()) {
            std::filesystem::create_directories(options.engine_cache_path);
        }
        Ort::SessionOptions session_options;
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

        if (options.provider == InferenceProvider::tensorrt) {
            const auto workspace = std::to_string(options.workspace_bytes);
            const auto cache_path = options.engine_cache_path.string();
            const char* keys[] = {
                "trt_engine_cache_enable",
                "trt_engine_cache_path",
                "trt_max_workspace_size",
                "trt_fp16_enable",
            };
            const char* values[] = {
                options.engine_cache ? "1" : "0",
                cache_path.c_str(),
                workspace.c_str(),
                options.fp16 ? "1" : "0",
            };
            Ort::ThrowOnError(Ort::GetApi().SessionOptionsAppendExecutionProvider_TensorRT_V2(
                session_options, keys, values, 4));
            impl_->provider = InferenceProvider::tensorrt;
        } else if (options.provider == InferenceProvider::cuda) {
            const auto device_id = std::string{"0"};
            const char* keys[] = {"device_id", "cudnn_conv_algo_search", "do_copy_in_default_stream"};
            const char* values[] = {device_id.c_str(), "EXHAUSTIVE", "1"};
            Ort::ThrowOnError(Ort::GetApi().SessionOptionsAppendExecutionProvider_CUDA_V2(
                session_options, keys, values, 3));
            impl_->provider = InferenceProvider::cuda;
        } else {
            error = "OnnxRuntimeSession requires TensorRT or CUDA provider";
            return false;
        }

        const auto path = manifest.model_path.wstring();
        impl_->session = std::make_unique<Ort::Session>(impl_->env, path.c_str(), session_options);
        impl_->manifest = manifest;
        impl_->loaded = true;
        return true;
    } catch (const Ort::Exception& exception) {
        error = exception.what();
        impl_->session.reset();
        impl_->loaded = false;
        return false;
    }
}

bool OnnxRuntimeSession::run(
    const std::vector<Tensor>& inputs,
    std::vector<Tensor>& outputs,
    std::string& error) {
    if (!impl_->loaded || !impl_->session) {
        error = "ONNX Runtime session is not loaded";
        return false;
    }
    if (inputs.empty()) {
        error = "ONNX Runtime requires at least one input tensor";
        return false;
    }
    try {
        Ort::AllocatorWithDefaultOptions allocator;
        std::vector<std::string> fallback_input_names;
        std::vector<std::string> fallback_output_names;
        const auto input_count = impl_->session->GetInputCount();
        const auto output_count = impl_->session->GetOutputCount();
        for (std::size_t index = 0; index < input_count; ++index) {
            auto name = impl_->session->GetInputNameAllocated(index, allocator);
            fallback_input_names.emplace_back(name.get());
        }
        for (std::size_t index = 0; index < output_count; ++index) {
            auto name = impl_->session->GetOutputNameAllocated(index, allocator);
            fallback_output_names.emplace_back(name.get());
        }
        const auto manifest_input_names = split_names(impl_->manifest.input_names);
        const auto manifest_output_names = split_names(impl_->manifest.output_names);
        const auto& selected_input_names = manifest_input_names.empty() ? fallback_input_names : manifest_input_names;
        const auto& selected_output_names = manifest_output_names.empty() ? fallback_output_names : manifest_output_names;
        if (selected_input_names.size() != inputs.size()) {
            error = "manifest input_names count does not match input tensors";
            return false;
        }
        std::vector<const char*> input_names;
        std::vector<const char*> output_names;
        for (const auto& name : selected_input_names) input_names.push_back(name.c_str());
        for (const auto& name : selected_output_names) output_names.push_back(name.c_str());

        // a sessão CUDA aceita tensores CPU e faz a transferência; zero-copy entra
        // quando as superfícies CUDA/D3D11 forem introduzidas.
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
            OrtAllocatorType::OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> input_values;
        input_values.reserve(inputs.size());
        for (const auto& input : inputs) {
            input_values.push_back(Ort::Value::CreateTensor<float>(
                memory_info,
                const_cast<float*>(input.values.data()),
                input.values.size(),
                input.shape.data(),
                input.shape.size()));
        }
        auto result = impl_->session->Run(
            Ort::RunOptions{nullptr},
            input_names.data(),
            input_values.data(),
            input_values.size(),
            output_names.data(),
            output_names.size());

        outputs.clear();
        outputs.reserve(result.size());
        for (auto& value : result) {
            auto type_info = value.GetTensorTypeAndShapeInfo();
            Tensor output;
            output.shape = type_info.GetShape();
            const auto count = type_info.GetElementCount();
            const auto* data = value.GetTensorData<float>();
            output.values.assign(data, data + count);
            outputs.push_back(std::move(output));
        }
        return true;
    } catch (const Ort::Exception& exception) {
        error = exception.what();
        return false;
    }
}

void OnnxRuntimeSession::close() noexcept {
    impl_->session.reset();
    impl_->loaded = false;
}

InferenceProvider OnnxRuntimeSession::provider() const noexcept {
    return impl_->provider;
}

} // namespace swave::core
