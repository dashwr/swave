#pragma once

namespace swave::core {

enum class PipelineState {
    disconnected,
    buffering,
    running,
    degraded,
    stopping,
    error,
};

} // namespace swave::core
