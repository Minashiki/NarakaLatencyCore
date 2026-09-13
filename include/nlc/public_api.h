#pragma once

#include <cstdint>

#if defined(_WIN32)
#if defined(NL_NATIVE_EXPORTS)
#define NL_API extern "C" __declspec(dllexport)
#else
#define NL_API extern "C" __declspec(dllimport)
#endif
#define NL_CALL __cdecl
#else
#define NL_API extern "C"
#define NL_CALL
#endif

enum NL_Result : std::int32_t {
    NL_OK = 0,
    NL_ERROR_INVALID_ARGUMENT = 1,
    NL_ERROR_INVALID_STATE = 2,
    NL_ERROR_ACCESS_DENIED = 3,
    NL_ERROR_DEPENDENCY_MISSING = 4,
    NL_ERROR_START_FAILED = 5,
    NL_ERROR_TIMEOUT = 6,
    NL_ERROR_BUFFER_TOO_SMALL = 7,
    NL_ERROR_VERSION_MISMATCH = 8,
    NL_ERROR_ALREADY_RUNNING = 9,
    NL_ERROR_INTERNAL = 10
};

enum NL_EngineState : std::int32_t {
    NL_STATE_STOPPED = 0,
    NL_STATE_STARTING = 1,
    NL_STATE_RUNNING = 2,
    NL_STATE_BYPASSING = 3,
    NL_STATE_STOPPING = 4,
    NL_STATE_FAULTED = 5
};

struct NL_DelaySettings {
    std::uint32_t structSize;
    std::uint8_t inboundEnabled;
    std::uint8_t outboundEnabled;
    std::uint8_t reserved0;
    std::uint8_t reserved1;
    std::int64_t inboundDelayUs;
    std::int64_t outboundDelayUs;
    std::uint64_t reserved2[4];
};

struct NL_DirectionMetrics {
    std::uint32_t structSize;
    std::uint8_t enabled;
    std::uint8_t reserved0[3];
    std::int64_t configuredDelayUs;
    std::uint64_t capturedPackets;
    std::uint64_t capturedBytes;
    std::uint64_t scheduledPackets;
    std::uint64_t scheduledBytes;
    std::uint64_t injectedPackets;
    std::uint64_t injectedBytes;
    std::uint64_t bypassPackets;
    std::uint64_t bypassBytes;
    std::uint64_t droppedPackets;
    std::uint64_t queueDepth;
    std::uint64_t maximumQueueDepth;
    double averageActualDelayUs;
    double p50ActualDelayUs;
    double p95ActualDelayUs;
    double p99ActualDelayUs;
    double maximumActualDelayUs;
    double averageSchedulingErrorUs;
    double p50SchedulingErrorUs;
    double p95SchedulingErrorUs;
    double p99SchedulingErrorUs;
    double maximumSchedulingErrorUs;
    std::uint64_t sendFailures;
    std::uint64_t poolExhaustions;
    std::uint64_t reserved1[4];
};

struct NL_MetricsSnapshot {
    std::uint32_t structSize;
    NL_EngineState engineState;
    std::uint64_t engineStartTimeUnixMs;
    std::uint64_t engineUptimeMs;
    std::uint64_t flushPendingPackets;
    double flushDurationUs;
    NL_DirectionMetrics inbound;
    NL_DirectionMetrics outbound;
    std::uint64_t currentPoolUsage;
    std::uint64_t maximumPoolUsage;
    std::uint64_t receiverErrors;
    std::uint64_t senderErrors;
    std::uint64_t consecutiveSendFailures;
    std::uint64_t csvRecordsDropped;
    std::uint64_t totalFlushCount;
    std::uint64_t totalStartCount;
    std::uint64_t totalStopCount;
    std::int32_t lastErrorCode;
    std::uint32_t reserved0;
    wchar_t activeFilter[128];
    wchar_t lastErrorText[512];
    std::uint64_t reserved1[8];
};

NL_API std::int32_t NL_CALL nl_initialize();
NL_API std::int32_t NL_CALL nl_start(const NL_DelaySettings* settings);
NL_API std::int32_t NL_CALL nl_update_settings(const NL_DelaySettings* settings);
NL_API std::int32_t NL_CALL nl_stop_and_flush(std::uint32_t timeoutMs);
NL_API std::int32_t NL_CALL nl_get_state(NL_EngineState* state);
NL_API std::int32_t NL_CALL nl_get_metrics(NL_MetricsSnapshot* metrics);
NL_API std::int32_t NL_CALL nl_get_last_error(wchar_t* buffer, std::uint32_t bufferLength);
NL_API std::int32_t NL_CALL nl_reset_metrics();
NL_API std::int32_t NL_CALL nl_set_csv_path(const wchar_t* path);
NL_API void NL_CALL nl_shutdown();

