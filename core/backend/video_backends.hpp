#pragma once

#include "core/frame/frame_packet.hpp"
#include "core/inference/inference_session.hpp"

#include <memory>
#include <string>

namespace swave::core {

class IUpscalerBackend {
public:
    virtual ~IUpscalerBackend() = default;
    [[nodiscard]] virtual bool initialize(const ModelManifest&, std::shared_ptr<IInferenceSession>, const InferenceOptions&, std::string&) = 0;
    [[nodiscard]] virtual bool process(const FramePacket&, FramePacket&, std::string&) = 0;
    virtual void flush() noexcept = 0;
    virtual void close() noexcept = 0;
};

class IInterpolatorBackend {
public:
    virtual ~IInterpolatorBackend() = default;
    [[nodiscard]] virtual bool initialize(const ModelManifest&, std::shared_ptr<IInferenceSession>, const InferenceOptions&, std::string&) = 0;
    [[nodiscard]] virtual bool process(const FramePacket&, const FramePacket&, double, FramePacket&, std::string&) = 0;
    virtual void flush() noexcept = 0;
    virtual void close() noexcept = 0;
};

class FakeUpscalerBackend final : public IUpscalerBackend {
public:
    [[nodiscard]] bool initialize(const ModelManifest&, std::shared_ptr<IInferenceSession>, const InferenceOptions&, std::string&) override;
    [[nodiscard]] bool process(const FramePacket&, FramePacket&, std::string&) override;
    void flush() noexcept override;
    void close() noexcept override;

private:
    std::shared_ptr<IInferenceSession> session_;
    bool initialized_{};
};

class FakeInterpolatorBackend final : public IInterpolatorBackend {
public:
    [[nodiscard]] bool initialize(const ModelManifest&, std::shared_ptr<IInferenceSession>, const InferenceOptions&, std::string&) override;
    [[nodiscard]] bool process(const FramePacket&, const FramePacket&, double, FramePacket&, std::string&) override;
    void flush() noexcept override;
    void close() noexcept override;

private:
    std::shared_ptr<IInferenceSession> session_;
    bool initialized_{};
};

class OnnxUpscalerBackend final : public IUpscalerBackend {
public:
    [[nodiscard]] bool initialize(const ModelManifest&, std::shared_ptr<IInferenceSession>, const InferenceOptions&, std::string&) override;
    [[nodiscard]] bool process(const FramePacket&, FramePacket&, std::string&) override;
    void flush() noexcept override;
    void close() noexcept override;

private:
    std::shared_ptr<IInferenceSession> session_;
    bool initialized_{};
};

class OnnxInterpolatorBackend final : public IInterpolatorBackend {
public:
    [[nodiscard]] bool initialize(const ModelManifest&, std::shared_ptr<IInferenceSession>, const InferenceOptions&, std::string&) override;
    [[nodiscard]] bool process(const FramePacket&, const FramePacket&, double, FramePacket&, std::string&) override;
    void flush() noexcept override;
    void close() noexcept override;

private:
    std::shared_ptr<IInferenceSession> session_;
    bool initialized_{};
};

} // namespace swave::core
