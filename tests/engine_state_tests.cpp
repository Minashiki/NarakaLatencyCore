#include "nlc/engine_state_machine.h"

#include <gtest/gtest.h>

#include <tuple>

namespace {

using TransitionCase = std::tuple<nlc::EngineState, nlc::EngineState, bool>;

class TransitionMatrixTest : public ::testing::TestWithParam<TransitionCase> {};

TEST_P(TransitionMatrixTest, MatchesDocumentedTransitionMatrix) {
    const auto [from, to, expected] = GetParam();
    EXPECT_EQ(nlc::IsAllowedTransition(from, to), expected);
}

INSTANTIATE_TEST_SUITE_P(
    AllStates,
    TransitionMatrixTest,
    ::testing::Values(
        TransitionCase{nlc::EngineState::Stopped, nlc::EngineState::Starting, true},
        TransitionCase{nlc::EngineState::Stopped, nlc::EngineState::Running, false},
        TransitionCase{nlc::EngineState::Stopped, nlc::EngineState::Stopping, false},
        TransitionCase{nlc::EngineState::Starting, nlc::EngineState::Running, true},
        TransitionCase{nlc::EngineState::Starting, nlc::EngineState::Faulted, true},
        TransitionCase{nlc::EngineState::Starting, nlc::EngineState::Starting, false},
        TransitionCase{nlc::EngineState::Running, nlc::EngineState::Bypassing, true},
        TransitionCase{nlc::EngineState::Running, nlc::EngineState::Stopping, true},
        TransitionCase{nlc::EngineState::Running, nlc::EngineState::Faulted, true},
        TransitionCase{nlc::EngineState::Running, nlc::EngineState::Starting, false},
        TransitionCase{nlc::EngineState::Bypassing, nlc::EngineState::Stopping, true},
        TransitionCase{nlc::EngineState::Bypassing, nlc::EngineState::Faulted, true},
        TransitionCase{nlc::EngineState::Bypassing, nlc::EngineState::Running, false},
        TransitionCase{nlc::EngineState::Stopping, nlc::EngineState::Stopped, true},
        TransitionCase{nlc::EngineState::Stopping, nlc::EngineState::Running, false},
        TransitionCase{nlc::EngineState::Faulted, nlc::EngineState::Stopping, true},
        TransitionCase{nlc::EngineState::Faulted, nlc::EngineState::Stopped, true},
        TransitionCase{nlc::EngineState::Faulted, nlc::EngineState::Running, false}));

TEST(EngineStateMachineTest, StartsStopped) {
    nlc::EngineStateMachine machine;
    EXPECT_EQ(machine.State(), nlc::EngineState::Stopped);
}

TEST(EngineStateMachineTest, PerformsNormalLifecycle) {
    nlc::EngineStateMachine machine;
    ASSERT_TRUE(machine.Transition(nlc::EngineState::Starting));
    ASSERT_TRUE(machine.Transition(nlc::EngineState::Running));
    ASSERT_TRUE(machine.Transition(nlc::EngineState::Stopping));
    ASSERT_TRUE(machine.Transition(nlc::EngineState::Stopped));
}

TEST(EngineStateMachineTest, RejectsIllegalTransitionWithoutChangingState) {
    nlc::EngineStateMachine machine;
    EXPECT_FALSE(machine.Transition(nlc::EngineState::Running));
    EXPECT_EQ(machine.State(), nlc::EngineState::Stopped);
}

TEST(EngineStateMachineTest, ObserverReceivesStructuredTransition) {
    int calls = 0;
    nlc::EngineState observedFrom{};
    nlc::EngineState observedTo{};
    nlc::EngineStateMachine machine([&](auto from, auto to, auto) {
        ++calls;
        observedFrom = from;
        observedTo = to;
    });
    ASSERT_TRUE(machine.Transition(nlc::EngineState::Starting, "test"));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(observedFrom, nlc::EngineState::Stopped);
    EXPECT_EQ(observedTo, nlc::EngineState::Starting);
}

}  // namespace
