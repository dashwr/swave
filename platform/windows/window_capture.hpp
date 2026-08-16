#pragma once

#ifdef _WIN32

#include "core/frame/frame_packet.hpp"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

namespace swave::platform {

struct WindowCaptureConfig {
    void* window{};
    std::size_t queue_capacity{4};
};

class WindowCaptureSource final {
public:
    explicit WindowCaptureSource(WindowCaptureConfig config);
    ~WindowCaptureSource();

    WindowCaptureSource(const WindowCaptureSource&) = delete;
    WindowCaptureSource& operator=(const WindowCaptureSource&) = delete;

    [[nodiscard]] bool start(std::string& error);
    void stop() noexcept;
    [[nodiscard]] bool try_receive(core::FramePacket& frame);
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::string last_error() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace swave::platform

#endif
