#pragma once

#include "nlc/delay_settings.h"
#include "nlc/engine_backend.h"
#include "nlc/engine_state_machine.h"
#include "nlc/rotating_logger.h"
#include "nlc/windivert_engine.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace nlc {

enum class ControllerResult : std::int32_t {
    Ok = 0,
    InvalidArgument = 1,
    InvalidState = 2,
    AccessDenied = 3,
    DependencyMissing = 4,
    StartFailed = 5,
    Timeout = 6,
    BufferTooSmall = 7,
    VersionMismatch = 8,
    AlreadyRunning = 9,
    InternalError = 10
};

struct ControllerSnapshot {
    EngineState engineState{EngineState::Stopped};
    std::uint64_t engineStartTimeUnixMs{0};
    std::uint64_t engineUptimeMs{0};
    std::string activeFilter{GlobalWinDivertFilter};
    ControllerResult lastErrorCode{ControllerResult::Ok};
    std::string lastErrorText{};
    std::uint64_t flushPendingPackets{0};
    double flushDurationUs{0.0};
    MetricsSnapshot metrics{};
};

class EngineController final {
public:
    using EngineFactory = std::function<std::unique_ptr<IEngineBackend>(
        DelaySettings&, const WinDivertEngineOptions&)>;

    explicit EngineController(
        EngineFactory factory = {},
        bool performEnvironmentChecks = true,
        std::filesystem::path logDirectory = DefaultLogDirectory());
    ~EngineController();

    EngineController(const EngineController&) = delete;
    EngineController& operator=(const EngineController&) = delete;

    [[nodiscard]] ControllerResult Start(const GlobalDelaySettings& settings) noexcept;
    [[nodiscard]] ControllerResult UpdateSettings(
        const GlobalDelaySettings& settings) noexcept;
    [[nodiscard]] ControllerResult StopAndFlush(std::uint32_t timeoutMs) noexcept;
    [[nodiscard]] ControllerSnapshot Snapshot() noexcept;
    [[nodiscard]] EngineState State() const noexcept;
    [[nodiscard]] std::string LastError() const;
    void ResetMetrics() noexcept;
    void SetCsvPath(std::string path);
    void Shutdown() noexcept;

private:
    [[nodiscard]] bool IsAdministrator() const noexcept;
    [[nodiscard]] bool DependenciesPresent(std::string& error) const;
    void SetError(ControllerResult code, std::string text);
    void JoinCompletedStopWorker();
    void LogTransition(EngineState from, EngineState to, std::string_view reason);

    EngineFactory factory_;
    bool performEnvironmentChecks_;
    DelaySettings liveSettings_;
    GlobalDelaySettings settings_{};
    EngineStateMachine stateMachine_;
    RotatingLogger logger_;

    mutable std::mutex apiMutex_;
    mutable std::mutex mutex_;
    std::condition_variable stopCondition_;
    std::unique_ptr<IEngineBackend> engine_;
    std::thread stopThread_;
    bool stopComplete_{true};
    MetricsSnapshot finalMetrics_{};
    std::string csvPath_{};
    ControllerResult lastErrorCode_{ControllerResult::Ok};
    std::string lastErrorText_{};
    std::chrono::system_clock::time_point startWallTime_{};
    std::chrono::steady_clock::time_point startMonotonic_{};
    std::uint64_t flushPendingPackets_{0};
    double flushDurationUs_{0.0};
    std::uint64_t totalFlushCount_{0};
    std::uint64_t totalStartCount_{0};
    std::uint64_t totalStopCount_{0};
    bool shutdownLogged_{false};
};

}  // namespace nlc
