#pragma once

#include "nlc/engine_state.h"

#include <functional>
#include <mutex>
#include <string_view>

namespace nlc {

class EngineStateMachine final {
public:
    using TransitionObserver =
        std::function<void(EngineState, EngineState, std::string_view)>;

    explicit EngineStateMachine(TransitionObserver observer = {});

    [[nodiscard]] EngineState State() const noexcept;
    [[nodiscard]] bool Transition(EngineState next, std::string_view reason = {});

private:
    mutable std::mutex mutex_;
    EngineState state_{EngineState::Stopped};
    TransitionObserver observer_;
};

}  // namespace nlc

