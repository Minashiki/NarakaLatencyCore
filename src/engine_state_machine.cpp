#include "nlc/engine_state_machine.h"

#include <utility>

namespace nlc {

EngineStateMachine::EngineStateMachine(TransitionObserver observer)
    : observer_(std::move(observer)) {
}

EngineState EngineStateMachine::State() const noexcept {
    std::lock_guard lock(mutex_);
    return state_;
}

bool EngineStateMachine::Transition(EngineState next, std::string_view reason) {
    TransitionObserver observer;
    EngineState previous;
    {
        std::lock_guard lock(mutex_);
        previous = state_;
        if (!IsAllowedTransition(previous, next)) {
            return false;
        }
        state_ = next;
        observer = observer_;
    }
    if (observer) {
        observer(previous, next, reason);
    }
    return true;
}

}  // namespace nlc
