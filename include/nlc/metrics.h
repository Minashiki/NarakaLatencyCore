#pragma once

#include "nlc/direction.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace nlc {

struct DirectionMetricsSnapshot {
    bool enabled{false};
    std::int64_t configuredDelayUs{0};
    std::uint64_t capturedPackets{0};
    std::uint64_t capturedBytes{0};
    std::uint64_t scheduledPackets{0};
    std::uint64_t scheduledBytes{0};
    std::uint64_t injectedPackets{0};
    std::uint64_t injectedBytes{0};
    std::uint64_t bypassPackets{0};
    std::uint64_t bypassBytes{0};
    std::uint64_t droppedPackets{0};
    std::uint64_t queueDepth{0};
    std::uint64_t maximumQueueDepth{0};
    std::uint64_t queuePressureEvents{0};
    double averageActualDelayUs{0.0};
    double p50ActualDelayUs{0.0};
    double p95ActualDelayUs{0.0};
    double p99ActualDelayUs{0.0};
    double maximumActualDelayUs{0.0};
    double averageSchedulingErrorUs{0.0};
    double p50SchedulingErrorUs{0.0};
    double p95SchedulingErrorUs{0.0};
    double p99SchedulingErrorUs{0.0};
    double maximumSchedulingErrorUs{0.0};
    std::uint64_t sendFailures{0};
    std::uint64_t poolExhaustions{0};
};

struct MetricsSnapshot {
    std::uint64_t capturedPackets{0};
    std::uint64_t injectedPackets{0};
    std::uint64_t droppedPackets{0};
    std::uint64_t queueDepth{0};
    std::uint64_t maximumQueueDepth{0};
    double averageSchedulingErrorUs{0.0};
    double p50SchedulingErrorUs{0.0};
    double p95SchedulingErrorUs{0.0};
    double p99SchedulingErrorUs{0.0};
    double maximumSchedulingErrorUs{0.0};
    std::uint64_t currentPoolUsage{0};
    std::uint64_t maximumPoolUsage{0};
    std::uint64_t receiverErrors{0};
    std::uint64_t senderErrors{0};
    std::uint64_t consecutiveSendFailures{0};
    std::uint64_t csvRecordsDropped{0};
    std::uint64_t totalFlushCount{0};
    std::uint64_t totalStartCount{0};
    std::uint64_t totalStopCount{0};
    std::array<DirectionMetricsSnapshot, 2> directions{};
};

class MetricsRegistry final {
public:
    explicit MetricsRegistry(std::size_t sampleCapacity = 65'536);

    void RecordCaptured(Direction direction, std::uint64_t bytes = 0) noexcept;
    void RecordScheduled(Direction direction, std::uint64_t bytes) noexcept;
    void RecordInjected(Direction direction, std::uint64_t bytes, double actualDelayUs);
    void RecordBypass(Direction direction, std::uint64_t bytes) noexcept;
    void RecordDropped(Direction direction) noexcept;
    void RecordQueuePush(Direction direction) noexcept;
    void RecordQueuePop(Direction direction) noexcept;
    void RecordQueuePressure(Direction direction) noexcept;
    void RecordSchedulingError(Direction direction, double errorUs);
    void RecordSendFailure(Direction direction) noexcept;
    void RecordPoolExhaustion(Direction direction) noexcept;
    void RecordReceiverError() noexcept;
    void RecordSenderError() noexcept;
    void SetConsecutiveSendFailures(std::uint64_t count) noexcept;
    void RecordCsvDropped() noexcept;
    void Reset() noexcept;

    [[nodiscard]] MetricsSnapshot Snapshot() const;
    [[nodiscard]] std::size_t SampleCapacity() const noexcept { return sampleCapacity_; }

private:
    struct DirectionState {
        std::atomic<std::uint64_t> captured{0};
        std::atomic<std::uint64_t> capturedBytes{0};
        std::atomic<std::uint64_t> scheduled{0};
        std::atomic<std::uint64_t> scheduledBytes{0};
        std::atomic<std::uint64_t> injected{0};
        std::atomic<std::uint64_t> injectedBytes{0};
        std::atomic<std::uint64_t> bypass{0};
        std::atomic<std::uint64_t> bypassBytes{0};
        std::atomic<std::uint64_t> dropped{0};
        std::atomic<std::uint64_t> queueDepth{0};
        std::atomic<std::uint64_t> maximumQueueDepth{0};
        std::atomic<std::uint64_t> queuePressureEvents{0};
        std::atomic<std::uint64_t> actualDelayCount{0};
        std::atomic<double> actualDelaySumUs{0.0};
        std::atomic<double> maximumActualDelayUs{0.0};
        std::atomic<std::uint64_t> schedulingCount{0};
        std::atomic<double> schedulingSumUs{0.0};
        std::atomic<double> maximumSchedulingErrorUs{0.0};
        mutable std::mutex samplesMutex;
        std::vector<double> actualDelaySamples;
        std::vector<double> schedulingSamples;
        std::size_t actualCursor{0};
        std::size_t schedulingCursor{0};
        std::atomic<std::uint64_t> sendFailures{0};
        std::atomic<std::uint64_t> poolExhaustions{0};
    };

    static void UpdateMaximum(std::atomic<std::uint64_t>& target, std::uint64_t value) noexcept;
    static void UpdateMaximum(std::atomic<double>& target, double value) noexcept;

    static void AddBoundedSample(
        std::vector<double>& samples,
        std::size_t& cursor,
        std::size_t capacity,
        double value);

    std::size_t sampleCapacity_;
    std::array<DirectionState, 2> states_;
    std::atomic<std::uint64_t> globalQueueDepth_{0};
    std::atomic<std::uint64_t> globalMaximumQueueDepth_{0};
    std::atomic<std::uint64_t> receiverErrors_{0};
    std::atomic<std::uint64_t> senderErrors_{0};
    std::atomic<std::uint64_t> consecutiveSendFailures_{0};
    std::atomic<std::uint64_t> csvRecordsDropped_{0};
};

}  // namespace nlc
