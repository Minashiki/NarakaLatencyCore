#include "nlc/public_api.h"

#include "nlc/engine_controller.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace {

std::mutex apiMutex;
std::unique_ptr<nlc::EngineController> controller;
HANDLE instanceMutex = nullptr;
bool ownsInstanceMutex = false;

std::int32_t Result(nlc::ControllerResult value) {
    return static_cast<std::int32_t>(value);
}

std::wstring ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const auto size = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return L"Unable to convert error text";
    }
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size);
    return output;
}

std::string ToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const auto size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string output(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        output.data(), size, nullptr, nullptr);
    return output;
}

bool Validate(const NL_DelaySettings* settings) {
    return settings != nullptr && settings->structSize >= sizeof(NL_DelaySettings);
}

nlc::GlobalDelaySettings Convert(const NL_DelaySettings& settings) {
    return nlc::GlobalDelaySettings{
        true,
        settings.inboundEnabled != 0,
        settings.outboundEnabled != 0,
        settings.inboundDelayUs,
        settings.outboundDelayUs};
}

void CopyWide(wchar_t* destination, std::size_t capacity, std::wstring_view value) {
    if (capacity == 0) {
        return;
    }
    const auto length = (std::min)(capacity - 1, value.size());
    std::wmemcpy(destination, value.data(), length);
    destination[length] = L'\0';
}

void FillDirection(
    NL_DirectionMetrics& output,
    const nlc::DirectionMetricsSnapshot& input) {
    output = {};
    output.structSize = sizeof(output);
    output.enabled = input.enabled ? 1 : 0;
    output.configuredDelayUs = input.configuredDelayUs;
    output.capturedPackets = input.capturedPackets;
    output.capturedBytes = input.capturedBytes;
    output.scheduledPackets = input.scheduledPackets;
    output.scheduledBytes = input.scheduledBytes;
    output.injectedPackets = input.injectedPackets;
    output.injectedBytes = input.injectedBytes;
    output.bypassPackets = input.bypassPackets;
    output.bypassBytes = input.bypassBytes;
    output.droppedPackets = input.droppedPackets;
    output.queueDepth = input.queueDepth;
    output.maximumQueueDepth = input.maximumQueueDepth;
    output.averageActualDelayUs = input.averageActualDelayUs;
    output.p50ActualDelayUs = input.p50ActualDelayUs;
    output.p95ActualDelayUs = input.p95ActualDelayUs;
    output.p99ActualDelayUs = input.p99ActualDelayUs;
    output.maximumActualDelayUs = input.maximumActualDelayUs;
    output.averageSchedulingErrorUs = input.averageSchedulingErrorUs;
    output.p50SchedulingErrorUs = input.p50SchedulingErrorUs;
    output.p95SchedulingErrorUs = input.p95SchedulingErrorUs;
    output.p99SchedulingErrorUs = input.p99SchedulingErrorUs;
    output.maximumSchedulingErrorUs = input.maximumSchedulingErrorUs;
    output.sendFailures = input.sendFailures;
    output.poolExhaustions = input.poolExhaustions;
}

std::int32_t EnsureInitialized() {
    if (!controller) {
        return NL_ERROR_INVALID_STATE;
    }
    return NL_OK;
}

void ReleaseInstanceMutex() {
    if (ownsInstanceMutex && instanceMutex != nullptr) {
        ReleaseMutex(instanceMutex);
        ownsInstanceMutex = false;
    }
}

}  // namespace

std::int32_t NL_CALL nl_initialize() {
    try {
        std::lock_guard lock(apiMutex);
        if (controller) {
            return NL_OK;
        }
        instanceMutex = CreateMutexW(
            nullptr, FALSE, L"Global\\NarakaLatencyController.Engine.v1");
        if (instanceMutex == nullptr) {
            return NL_ERROR_INTERNAL;
        }
        controller = std::make_unique<nlc::EngineController>();
        return NL_OK;
    } catch (...) {
        return NL_ERROR_INTERNAL;
    }
}

std::int32_t NL_CALL nl_start(const NL_DelaySettings* settings) {
    try {
        std::lock_guard lock(apiMutex);
        if (const auto initialized = EnsureInitialized(); initialized != NL_OK) {
            return initialized;
        }
        if (settings == nullptr) {
            return NL_ERROR_INVALID_ARGUMENT;
        }
        if (!Validate(settings)) {
            return NL_ERROR_VERSION_MISMATCH;
        }
        if (!ownsInstanceMutex) {
            const auto wait = WaitForSingleObject(instanceMutex, 0);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
                return NL_ERROR_ALREADY_RUNNING;
            }
            ownsInstanceMutex = true;
        }
        const auto result = controller->Start(Convert(*settings));
        if (result != nlc::ControllerResult::Ok) {
            ReleaseInstanceMutex();
        }
        return Result(result);
    } catch (...) {
        ReleaseInstanceMutex();
        return NL_ERROR_INTERNAL;
    }
}

std::int32_t NL_CALL nl_update_settings(const NL_DelaySettings* settings) {
    try {
        std::lock_guard lock(apiMutex);
        if (const auto initialized = EnsureInitialized(); initialized != NL_OK) {
            return initialized;
        }
        if (settings == nullptr) {
            return NL_ERROR_INVALID_ARGUMENT;
        }
        if (!Validate(settings)) {
            return NL_ERROR_VERSION_MISMATCH;
        }
        return Result(controller->UpdateSettings(Convert(*settings)));
    } catch (...) {
        return NL_ERROR_INTERNAL;
    }
}

std::int32_t NL_CALL nl_stop_and_flush(std::uint32_t timeoutMs) {
    try {
        std::lock_guard lock(apiMutex);
        if (const auto initialized = EnsureInitialized(); initialized != NL_OK) {
            return initialized;
        }
        const auto result = controller->StopAndFlush(timeoutMs);
        if (result == nlc::ControllerResult::Ok) {
            ReleaseInstanceMutex();
        }
        return Result(result);
    } catch (...) {
        return NL_ERROR_INTERNAL;
    }
}

std::int32_t NL_CALL nl_get_state(NL_EngineState* state) {
    try {
        std::lock_guard lock(apiMutex);
        if (state == nullptr) {
            return NL_ERROR_INVALID_ARGUMENT;
        }
        if (const auto initialized = EnsureInitialized(); initialized != NL_OK) {
            return initialized;
        }
        *state = static_cast<NL_EngineState>(controller->State());
        return NL_OK;
    } catch (...) {
        return NL_ERROR_INTERNAL;
    }
}

std::int32_t NL_CALL nl_get_metrics(NL_MetricsSnapshot* metrics) {
    try {
        std::lock_guard lock(apiMutex);
        if (metrics == nullptr) {
            return NL_ERROR_INVALID_ARGUMENT;
        }
        if (metrics->structSize < sizeof(NL_MetricsSnapshot)) {
            return NL_ERROR_VERSION_MISMATCH;
        }
        if (const auto initialized = EnsureInitialized(); initialized != NL_OK) {
            return initialized;
        }
        const auto snapshot = controller->Snapshot();
        *metrics = {};
        metrics->structSize = sizeof(*metrics);
        metrics->engineState = static_cast<NL_EngineState>(snapshot.engineState);
        metrics->engineStartTimeUnixMs = snapshot.engineStartTimeUnixMs;
        metrics->engineUptimeMs = snapshot.engineUptimeMs;
        metrics->flushPendingPackets = snapshot.flushPendingPackets;
        metrics->flushDurationUs = snapshot.flushDurationUs;
        FillDirection(metrics->inbound, snapshot.metrics.directions[0]);
        FillDirection(metrics->outbound, snapshot.metrics.directions[1]);
        metrics->currentPoolUsage = snapshot.metrics.currentPoolUsage;
        metrics->maximumPoolUsage = snapshot.metrics.maximumPoolUsage;
        metrics->receiverErrors = snapshot.metrics.receiverErrors;
        metrics->senderErrors = snapshot.metrics.senderErrors;
        metrics->consecutiveSendFailures = snapshot.metrics.consecutiveSendFailures;
        metrics->csvRecordsDropped = snapshot.metrics.csvRecordsDropped;
        metrics->totalFlushCount = snapshot.metrics.totalFlushCount;
        metrics->totalStartCount = snapshot.metrics.totalStartCount;
        metrics->totalStopCount = snapshot.metrics.totalStopCount;
        metrics->lastErrorCode = static_cast<std::int32_t>(snapshot.lastErrorCode);
        CopyWide(metrics->activeFilter, std::size(metrics->activeFilter),
                 ToWide(snapshot.activeFilter));
        CopyWide(metrics->lastErrorText, std::size(metrics->lastErrorText),
                 ToWide(snapshot.lastErrorText));
        return NL_OK;
    } catch (...) {
        return NL_ERROR_INTERNAL;
    }
}

std::int32_t NL_CALL nl_get_last_error(wchar_t* buffer, std::uint32_t bufferLength) {
    try {
        std::lock_guard lock(apiMutex);
        if (buffer == nullptr || bufferLength == 0) {
            return NL_ERROR_INVALID_ARGUMENT;
        }
        if (const auto initialized = EnsureInitialized(); initialized != NL_OK) {
            buffer[0] = L'\0';
            return initialized;
        }
        const auto error = ToWide(controller->LastError());
        if (error.size() + 1 > bufferLength) {
            buffer[0] = L'\0';
            return NL_ERROR_BUFFER_TOO_SMALL;
        }
        CopyWide(buffer, bufferLength, error);
        return NL_OK;
    } catch (...) {
        return NL_ERROR_INTERNAL;
    }
}

std::int32_t NL_CALL nl_reset_metrics() {
    try {
        std::lock_guard lock(apiMutex);
        if (const auto initialized = EnsureInitialized(); initialized != NL_OK) {
            return initialized;
        }
        controller->ResetMetrics();
        return NL_OK;
    } catch (...) {
        return NL_ERROR_INTERNAL;
    }
}

std::int32_t NL_CALL nl_set_csv_path(const wchar_t* path) {
    try {
        std::lock_guard lock(apiMutex);
        if (path == nullptr) {
            return NL_ERROR_INVALID_ARGUMENT;
        }
        if (const auto initialized = EnsureInitialized(); initialized != NL_OK) {
            return initialized;
        }
        controller->SetCsvPath(ToUtf8(path));
        return NL_OK;
    } catch (const std::logic_error&) {
        return NL_ERROR_INVALID_STATE;
    } catch (...) {
        return NL_ERROR_INTERNAL;
    }
}

void NL_CALL nl_shutdown() {
    try {
        std::lock_guard lock(apiMutex);
        if (controller) {
            controller->Shutdown();
            controller.reset();
        }
        ReleaseInstanceMutex();
        if (instanceMutex != nullptr) {
            CloseHandle(instanceMutex);
            instanceMutex = nullptr;
        }
    } catch (...) {
    }
}
