#pragma once

#include <cstdint>

namespace nlc {

enum class EngineState : std::int32_t {
    Stopped = 0,
    Starting = 1,
    Running = 2,
    Bypassing = 3,
    Stopping = 4,
    Faulted = 5
};

[[nodiscard]] constexpr const char* EngineStateName(EngineState state) noexcept {
    switch (state) {
    case EngineState::Stopped: return "Stopped";
    case EngineState::Starting: return "Starting";
    case EngineState::Running: return "Running";
    case EngineState::Bypassing: return "Bypassing";
    case EngineState::Stopping: return "Stopping";
    case EngineState::Faulted: return "Faulted";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool IsAllowedTransition(
    EngineState from,
    EngineState to) noexcept {
    switch (from) {
    case EngineState::Stopped:
        return to == EngineState::Starting;
    case EngineState::Starting:
        return to == EngineState::Running || to == EngineState::Faulted;
    case EngineState::Running:
        return to == EngineState::Bypassing || to == EngineState::Stopping ||
            to == EngineState::Faulted;
    case EngineState::Bypassing:
        return to == EngineState::Stopping || to == EngineState::Faulted;
    case EngineState::Stopping:
        return to == EngineState::Stopped;
    case EngineState::Faulted:
        return to == EngineState::Stopping || to == EngineState::Stopped;
    }
    return false;
}

}  // namespace nlc

