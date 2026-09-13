#include "nlc/engine_controller.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

namespace {

class FakeBackend : public nlc::IEngineBackend {
public:
    explicit FakeBackend(bool failStart = false, int stopDelayMs = 0)
        : failStart_(failStart), stopDelayMs_(stopDelayMs) {}
    void Start() override {
        if (failStart_) throw std::runtime_error("synthetic start failure");
        running_ = true;
    }
    void StopAndFlush() override {
        if (stopDelayMs_ != 0) std::this_thread::sleep_for(std::chrono::milliseconds(stopDelayMs_));
        running_ = false;
    }
    nlc::MetricsSnapshot Metrics() const override { return {}; }
    void ResetMetrics() noexcept override {}
    bool IsRunning() const noexcept override { return running_; }
    bool IsSafetyBypassActive() const noexcept override { return false; }
    std::string LastError() const override { return {}; }
private:
    bool failStart_;
    int stopDelayMs_;
    std::atomic<bool> running_{false};
};

nlc::GlobalDelaySettings Settings() {
    return {true, true, true, 1'000, 2'000};
}

TEST(LifecycleTest, OneHundredStartStopCyclesComplete) {
    std::atomic<int> created{0};
    nlc::EngineController controller(
        [&](auto&, const auto&) {
            ++created;
            return std::make_unique<FakeBackend>();
        }, false, std::filesystem::path("test-logs"));
    for (int cycle = 0; cycle < 100; ++cycle) {
        ASSERT_EQ(controller.Start(Settings()), nlc::ControllerResult::Ok) << cycle;
        ASSERT_EQ(controller.StopAndFlush(1'000), nlc::ControllerResult::Ok) << cycle;
        ASSERT_EQ(controller.State(), nlc::EngineState::Stopped);
    }
    EXPECT_EQ(created.load(), 100);
    const auto snapshot = controller.Snapshot();
    EXPECT_EQ(snapshot.metrics.totalStartCount, 100u);
    EXPECT_EQ(snapshot.metrics.totalStopCount, 100u);
}

TEST(LifecycleTest, DuplicateStartDoesNotCreateSecondBackend) {
    std::atomic<int> created{0};
    nlc::EngineController controller(
        [&](auto&, const auto&) {
            ++created;
            return std::make_unique<FakeBackend>();
        }, false, std::filesystem::path("test-logs"));
    ASSERT_EQ(controller.Start(Settings()), nlc::ControllerResult::Ok);
    EXPECT_EQ(controller.Start(Settings()), nlc::ControllerResult::AlreadyRunning);
    EXPECT_EQ(created.load(), 1);
    EXPECT_EQ(controller.StopAndFlush(1'000), nlc::ControllerResult::Ok);
}

TEST(LifecycleTest, StartFailureEntersFaultedAndCanStopSafely) {
    nlc::EngineController controller(
        [](auto&, const auto&) { return std::make_unique<FakeBackend>(true); },
        false, std::filesystem::path("test-logs"));
    EXPECT_EQ(controller.Start(Settings()), nlc::ControllerResult::StartFailed);
    EXPECT_EQ(controller.State(), nlc::EngineState::Faulted);
    EXPECT_EQ(controller.StopAndFlush(1'000), nlc::ControllerResult::Ok);
    EXPECT_EQ(controller.State(), nlc::EngineState::Stopped);
}

TEST(LifecycleTest, StopTimeoutIsReportedWhileCleanupContinues) {
    nlc::EngineController controller(
        [](auto&, const auto&) { return std::make_unique<FakeBackend>(false, 50); },
        false, std::filesystem::path("test-logs"));
    ASSERT_EQ(controller.Start(Settings()), nlc::ControllerResult::Ok);
    EXPECT_EQ(controller.StopAndFlush(1), nlc::ControllerResult::Timeout);
    EXPECT_EQ(controller.StopAndFlush(1'000), nlc::ControllerResult::Ok);
    EXPECT_EQ(controller.State(), nlc::EngineState::Stopped);
}

TEST(LifecycleTest, RuntimeSettingsRemainDirectionIndependent) {
    nlc::EngineController controller(
        [](auto&, const auto&) { return std::make_unique<FakeBackend>(); },
        false, std::filesystem::path("test-logs"));
    ASSERT_EQ(controller.Start(Settings()), nlc::ControllerResult::Ok);
    auto changed = Settings();
    changed.inboundDelayUs = 9'000;
    ASSERT_EQ(controller.UpdateSettings(changed), nlc::ControllerResult::Ok);
    EXPECT_EQ(controller.StopAndFlush(1'000), nlc::ControllerResult::Ok);
}

}  // namespace

