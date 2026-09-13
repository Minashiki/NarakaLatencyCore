#include "nlc/engine_controller.h"

#include <windows.h>

#include <filesystem>
#include <sstream>
#include <system_error>

namespace nlc {
namespace {

std::unique_ptr<IEngineBackend> DefaultEngineFactory(
    DelaySettings& settings,
    const WinDivertEngineOptions& options) {
    return std::make_unique<WinDivertEngine>(settings, options);
}

std::uint64_t UnixMilliseconds(std::chrono::system_clock::time_point value) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        value.time_since_epoch()).count());
}

}  // namespace

EngineController::EngineController(
    EngineFactory factory,
    bool performEnvironmentChecks,
    std::filesystem::path logDirectory)
    : factory_(factory ? std::move(factory) : DefaultEngineFactory),
      performEnvironmentChecks_(performEnvironmentChecks),
      stateMachine_([this](EngineState from, EngineState to, std::string_view reason) {
          LogTransition(from, to, reason);
      }),
      logger_(std::move(logDirectory)) {
    logger_.Write("controller.initialize", "native controller initialized");
}

EngineController::~EngineController() {
    Shutdown();
}

ControllerResult EngineController::Start(const GlobalDelaySettings& settings) noexcept {
    std::lock_guard apiLock(apiMutex_);
    try {
        JoinCompletedStopWorker();
        if (stateMachine_.State() != EngineState::Stopped) {
            SetError(ControllerResult::AlreadyRunning, "engine is not stopped");
            return ControllerResult::AlreadyRunning;
        }
        std::string validationError;
        if (!ValidateSettings(settings, &validationError)) {
            SetError(ControllerResult::InvalidArgument, validationError);
            return ControllerResult::InvalidArgument;
        }
        if (!stateMachine_.Transition(EngineState::Starting, "start requested")) {
            SetError(ControllerResult::InvalidState, "illegal transition to Starting");
            return ControllerResult::InvalidState;
        }

        if (performEnvironmentChecks_ && !IsAdministrator()) {
            SetError(
                ControllerResult::AccessDenied,
                "administrator privileges are required to load WinDivert");
            (void)stateMachine_.Transition(EngineState::Faulted, "administrator check failed");
            return ControllerResult::AccessDenied;
        }
        std::string dependencyError;
        if (performEnvironmentChecks_ && !DependenciesPresent(dependencyError)) {
            SetError(ControllerResult::DependencyMissing, dependencyError);
            (void)stateMachine_.Transition(EngineState::Faulted, "dependency check failed");
            return ControllerResult::DependencyMissing;
        }

        liveSettings_.Store(settings);
        WinDivertEngineOptions options;
        options.filter = GlobalWinDivertFilter;
        {
            std::lock_guard lock(mutex_);
            options.scheduler.csvPath = csvPath_;
        }
        auto backend = factory_(liveSettings_, options);
        backend->Start();
        {
            std::lock_guard lock(mutex_);
            engine_ = std::move(backend);
            settings_ = settings;
            settings_.engineEnabled = true;
            finalMetrics_ = {};
            startWallTime_ = std::chrono::system_clock::now();
            startMonotonic_ = std::chrono::steady_clock::now();
            ++totalStartCount_;
            lastErrorCode_ = ControllerResult::Ok;
            lastErrorText_.clear();
        }
        (void)stateMachine_.Transition(EngineState::Running, "engine threads started");
        logger_.Write("filter.active", GlobalWinDivertFilter);
        return ControllerResult::Ok;
    } catch (const std::exception& error) {
        SetError(ControllerResult::StartFailed, error.what());
    } catch (...) {
        SetError(ControllerResult::StartFailed, "unknown start failure");
    }
    if (stateMachine_.State() == EngineState::Starting) {
        (void)stateMachine_.Transition(EngineState::Faulted, "start failed");
    }
    return ControllerResult::StartFailed;
}

ControllerResult EngineController::UpdateSettings(
    const GlobalDelaySettings& settings) noexcept {
    std::lock_guard apiLock(apiMutex_);
    try {
        std::string validationError;
        if (!ValidateSettings(settings, &validationError)) {
            SetError(ControllerResult::InvalidArgument, validationError);
            return ControllerResult::InvalidArgument;
        }
        const auto state = stateMachine_.State();
        if (state != EngineState::Running && state != EngineState::Bypassing) {
            SetError(ControllerResult::InvalidState, "settings can only change while running");
            return ControllerResult::InvalidState;
        }
        liveSettings_.Store(settings);
        {
            std::lock_guard lock(mutex_);
            settings_ = settings;
            settings_.engineEnabled = true;
        }
        std::ostringstream message;
        message << "inboundEnabled=" << settings.inboundEnabled
                << ",inboundDelayUs=" << settings.inboundDelayUs
                << ",outboundEnabled=" << settings.outboundEnabled
                << ",outboundDelayUs=" << settings.outboundDelayUs;
        logger_.Write("settings.update", message.str());
        return ControllerResult::Ok;
    } catch (...) {
        SetError(ControllerResult::InternalError, "settings update failed");
        return ControllerResult::InternalError;
    }
}

ControllerResult EngineController::StopAndFlush(std::uint32_t timeoutMs) noexcept {
    std::unique_lock apiLock(apiMutex_);
    try {
        JoinCompletedStopWorker();
        auto state = stateMachine_.State();
        if (state == EngineState::Stopped) {
            return ControllerResult::Ok;
        }
        if (state == EngineState::Starting) {
            SetError(ControllerResult::InvalidState, "cannot stop during synchronous start");
            return ControllerResult::InvalidState;
        }

        if (state != EngineState::Stopping) {
            if (!stateMachine_.Transition(EngineState::Stopping, "safe stop requested")) {
                SetError(ControllerResult::InvalidState, "illegal transition to Stopping");
                return ControllerResult::InvalidState;
            }
            std::unique_ptr<IEngineBackend> backend;
            {
                std::lock_guard lock(mutex_);
                if (engine_) {
                    finalMetrics_ = engine_->Metrics();
                    flushPendingPackets_ = finalMetrics_.queueDepth;
                }
                backend = std::move(engine_);
                stopComplete_ = false;
            }
            MetricsSnapshot baselineMetrics;
            {
                std::lock_guard lock(mutex_);
                baselineMetrics = finalMetrics_;
            }
            stopThread_ = std::thread([
                this, backend = std::move(backend), baselineMetrics]() mutable {
                const auto started = std::chrono::steady_clock::now();
                auto stoppedMetrics = baselineMetrics;
                try {
                    if (backend) {
                        backend->StopAndFlush();
                        stoppedMetrics = backend->Metrics();
                    }
                } catch (const std::exception& error) {
                    SetError(ControllerResult::InternalError, error.what());
                } catch (...) {
                    SetError(ControllerResult::InternalError, "unknown stop failure");
                }
                {
                    std::lock_guard lock(mutex_);
                    finalMetrics_ = stoppedMetrics;
                    flushDurationUs_ = std::chrono::duration<double, std::micro>(
                        std::chrono::steady_clock::now() - started).count();
                    flushPendingPackets_ = finalMetrics_.queueDepth;
                    ++totalFlushCount_;
                    ++totalStopCount_;
                    settings_.engineEnabled = false;
                    stopComplete_ = true;
                }
                (void)stateMachine_.Transition(EngineState::Stopped, "safe stop completed");
                stopCondition_.notify_all();
            });
        }

        apiLock.unlock();
        std::unique_lock lock(mutex_);
        const auto completed = stopCondition_.wait_for(
            lock, std::chrono::milliseconds(timeoutMs), [this] { return stopComplete_; });
        lock.unlock();
        if (!completed) {
            SetError(ControllerResult::Timeout, "safe stop is still in progress");
            return ControllerResult::Timeout;
        }
        JoinCompletedStopWorker();
        return ControllerResult::Ok;
    } catch (...) {
        SetError(ControllerResult::InternalError, "stop failed");
        return ControllerResult::InternalError;
    }
}

ControllerSnapshot EngineController::Snapshot() noexcept {
    ControllerSnapshot result;
    try {
        result.engineState = stateMachine_.State();
        bool bypassActive = false;
        bool backendRunning = true;
        std::string backendError;
        {
            std::lock_guard lock(mutex_);
            result.engineStartTimeUnixMs = startWallTime_.time_since_epoch().count() == 0
                ? 0 : UnixMilliseconds(startWallTime_);
            if (result.engineStartTimeUnixMs != 0 && result.engineState != EngineState::Stopped) {
                result.engineUptimeMs = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - startMonotonic_).count());
            }
            result.lastErrorCode = lastErrorCode_;
            result.lastErrorText = lastErrorText_;
            result.flushPendingPackets = flushPendingPackets_;
            result.flushDurationUs = flushDurationUs_;
            result.metrics = engine_ ? engine_->Metrics() : finalMetrics_;
            if (engine_) {
                bypassActive = engine_->IsSafetyBypassActive();
                backendRunning = engine_->IsRunning();
                backendError = engine_->LastError();
            }
            result.metrics.totalFlushCount = totalFlushCount_;
            result.metrics.totalStartCount = totalStartCount_;
            result.metrics.totalStopCount = totalStopCount_;
        }
        if (result.engineState == EngineState::Running) {
            if (bypassActive) {
                SetError(ControllerResult::InternalError, "engine entered safety bypass");
                (void)stateMachine_.Transition(EngineState::Bypassing, "safety condition detected");
                result.engineState = EngineState::Bypassing;
            } else if (!backendRunning) {
                SetError(ControllerResult::InternalError,
                         backendError.empty() ? "engine receiver stopped unexpectedly" : backendError);
                (void)stateMachine_.Transition(EngineState::Faulted, "engine stopped unexpectedly");
                result.engineState = EngineState::Faulted;
            }
        }
    } catch (...) {
        SetError(ControllerResult::InternalError, "metrics snapshot failed");
        result.engineState = stateMachine_.State();
        result.lastErrorCode = ControllerResult::InternalError;
        result.lastErrorText = "metrics snapshot failed";
    }
    return result;
}

EngineState EngineController::State() const noexcept {
    return stateMachine_.State();
}

std::string EngineController::LastError() const {
    std::lock_guard lock(mutex_);
    return lastErrorText_;
}

void EngineController::ResetMetrics() noexcept {
    try {
        std::lock_guard lock(mutex_);
        if (engine_) {
            engine_->ResetMetrics();
        }
        finalMetrics_ = {};
        totalFlushCount_ = 0;
        totalStartCount_ = 0;
        totalStopCount_ = 0;
        logger_.Write("metrics.reset", "metrics counters reset");
    } catch (...) {
        SetError(ControllerResult::InternalError, "metrics reset failed");
    }
}

void EngineController::SetCsvPath(std::string path) {
    std::lock_guard apiLock(apiMutex_);
    if (stateMachine_.State() != EngineState::Stopped) {
        throw std::logic_error("CSV path can only change while stopped");
    }
    std::lock_guard lock(mutex_);
    csvPath_ = std::move(path);
}

void EngineController::Shutdown() noexcept {
    (void)StopAndFlush(30'000);
    if (stopThread_.joinable()) {
        stopThread_.join();
    }
    bool shouldLog = false;
    {
        std::lock_guard lock(mutex_);
        if (!shutdownLogged_) {
            shutdownLogged_ = true;
            shouldLog = true;
        }
    }
    if (shouldLog) {
        logger_.Write("controller.shutdown", "native controller shut down");
    }
}

bool EngineController::IsAdministrator() const noexcept {
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = nullptr;
    if (!AllocateAndInitializeSid(
            &authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &administrators)) {
        return false;
    }
    BOOL member = FALSE;
    const auto ok = CheckTokenMembership(nullptr, administrators, &member);
    FreeSid(administrators);
    return ok && member;
}

bool EngineController::DependenciesPresent(std::string& error) const {
    wchar_t modulePath[32'768]{};
    const auto length = GetModuleFileNameW(nullptr, modulePath, std::size(modulePath));
    if (length == 0 || length >= std::size(modulePath)) {
        error = "unable to locate application directory";
        return false;
    }
    const auto directory = std::filesystem::path(modulePath).parent_path();
    if (!std::filesystem::exists(directory / L"WinDivert.dll")) {
        error = "WinDivert.dll is missing beside the application";
        return false;
    }
    if (!std::filesystem::exists(directory / L"WinDivert64.sys")) {
        error = "WinDivert64.sys is missing beside the application";
        return false;
    }
    return true;
}

void EngineController::SetError(ControllerResult code, std::string text) {
    {
        std::lock_guard lock(mutex_);
        lastErrorCode_ = code;
        lastErrorText_ = text;
    }
    logger_.Write("controller.error", text);
}

void EngineController::JoinCompletedStopWorker() {
    bool complete = false;
    {
        std::lock_guard lock(mutex_);
        complete = stopComplete_;
    }
    if (complete && stopThread_.joinable()) {
        stopThread_.join();
    }
}

void EngineController::LogTransition(
    EngineState from,
    EngineState to,
    std::string_view reason) {
    std::ostringstream message;
    message << "from=" << EngineStateName(from) << ",to=" << EngineStateName(to)
            << ",reason=" << reason;
    logger_.Write("engine.state", message.str());
}

}  // namespace nlc
