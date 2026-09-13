#include "nlc/metrics.h"

#include <algorithm>
#include <cmath>

namespace nlc {
namespace {

double Percentile(const std::vector<double>& sorted, double percentile) {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto rank = static_cast<std::size_t>(
        std::ceil(percentile * static_cast<double>(sorted.size())));
    const auto index = (std::max<std::size_t>)(1, rank) - 1;
    return sorted[(std::min)(index, sorted.size() - 1)];
}

}  // namespace

MetricsRegistry::MetricsRegistry(std::size_t sampleCapacity)
    : sampleCapacity_((std::max<std::size_t>)(1, sampleCapacity)) {
    for (auto& state : states_) {
        state.actualDelaySamples.reserve(sampleCapacity_);
        state.schedulingSamples.reserve(sampleCapacity_);
    }
}

void MetricsRegistry::RecordCaptured(Direction direction, std::uint64_t bytes) noexcept {
    auto& state = states_[DirectionIndex(direction)];
    state.captured.fetch_add(1, std::memory_order_relaxed);
    state.capturedBytes.fetch_add(bytes, std::memory_order_relaxed);
}

void MetricsRegistry::RecordScheduled(Direction direction, std::uint64_t bytes) noexcept {
    auto& state = states_[DirectionIndex(direction)];
    state.scheduled.fetch_add(1, std::memory_order_relaxed);
    state.scheduledBytes.fetch_add(bytes, std::memory_order_relaxed);
}

void MetricsRegistry::RecordInjected(
    Direction direction,
    std::uint64_t bytes,
    double actualDelayUs) {
    auto& state = states_[DirectionIndex(direction)];
    state.injected.fetch_add(1, std::memory_order_relaxed);
    state.injectedBytes.fetch_add(bytes, std::memory_order_relaxed);
    state.actualDelayCount.fetch_add(1, std::memory_order_relaxed);
    state.actualDelaySumUs.fetch_add(actualDelayUs, std::memory_order_relaxed);
    UpdateMaximum(state.maximumActualDelayUs, actualDelayUs);
    std::lock_guard lock(state.samplesMutex);
    AddBoundedSample(
        state.actualDelaySamples, state.actualCursor, sampleCapacity_, actualDelayUs);
}

void MetricsRegistry::RecordBypass(Direction direction, std::uint64_t bytes) noexcept {
    auto& state = states_[DirectionIndex(direction)];
    state.bypass.fetch_add(1, std::memory_order_relaxed);
    state.bypassBytes.fetch_add(bytes, std::memory_order_relaxed);
}

void MetricsRegistry::RecordDropped(Direction direction) noexcept {
    states_[DirectionIndex(direction)].dropped.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::RecordQueuePush(Direction direction) noexcept {
    auto& state = states_[DirectionIndex(direction)];
    const auto depth = state.queueDepth.fetch_add(1, std::memory_order_relaxed) + 1;
    UpdateMaximum(state.maximumQueueDepth, depth);
    const auto globalDepth = globalQueueDepth_.fetch_add(1, std::memory_order_relaxed) + 1;
    UpdateMaximum(globalMaximumQueueDepth_, globalDepth);
}

void MetricsRegistry::RecordQueuePop(Direction direction) noexcept {
    states_[DirectionIndex(direction)].queueDepth.fetch_sub(1, std::memory_order_relaxed);
    globalQueueDepth_.fetch_sub(1, std::memory_order_relaxed);
}

void MetricsRegistry::RecordQueuePressure(Direction direction) noexcept {
    states_[DirectionIndex(direction)].queuePressureEvents.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::RecordSchedulingError(Direction direction, double errorUs) {
    auto& state = states_[DirectionIndex(direction)];
    const auto nonNegativeError = (std::max)(0.0, errorUs);
    state.schedulingCount.fetch_add(1, std::memory_order_relaxed);
    state.schedulingSumUs.fetch_add(nonNegativeError, std::memory_order_relaxed);
    UpdateMaximum(state.maximumSchedulingErrorUs, nonNegativeError);
    std::lock_guard lock(state.samplesMutex);
    AddBoundedSample(
        state.schedulingSamples, state.schedulingCursor, sampleCapacity_, nonNegativeError);
}

void MetricsRegistry::RecordSendFailure(Direction direction) noexcept {
    states_[DirectionIndex(direction)].sendFailures.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::RecordPoolExhaustion(Direction direction) noexcept {
    states_[DirectionIndex(direction)].poolExhaustions.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::RecordReceiverError() noexcept {
    receiverErrors_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::RecordSenderError() noexcept {
    senderErrors_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::SetConsecutiveSendFailures(std::uint64_t count) noexcept {
    consecutiveSendFailures_.store(count, std::memory_order_relaxed);
}

void MetricsRegistry::RecordCsvDropped() noexcept {
    csvRecordsDropped_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::Reset() noexcept {
    for (auto& state : states_) {
        state.captured.store(0, std::memory_order_relaxed);
        state.capturedBytes.store(0, std::memory_order_relaxed);
        state.scheduled.store(0, std::memory_order_relaxed);
        state.scheduledBytes.store(0, std::memory_order_relaxed);
        state.injected.store(0, std::memory_order_relaxed);
        state.injectedBytes.store(0, std::memory_order_relaxed);
        state.bypass.store(0, std::memory_order_relaxed);
        state.bypassBytes.store(0, std::memory_order_relaxed);
        state.dropped.store(0, std::memory_order_relaxed);
        state.maximumQueueDepth.store(
            state.queueDepth.load(std::memory_order_relaxed), std::memory_order_relaxed);
        state.queuePressureEvents.store(0, std::memory_order_relaxed);
        state.actualDelayCount.store(0, std::memory_order_relaxed);
        state.actualDelaySumUs.store(0.0, std::memory_order_relaxed);
        state.maximumActualDelayUs.store(0.0, std::memory_order_relaxed);
        state.schedulingCount.store(0, std::memory_order_relaxed);
        state.schedulingSumUs.store(0.0, std::memory_order_relaxed);
        state.maximumSchedulingErrorUs.store(0.0, std::memory_order_relaxed);
        state.sendFailures.store(0, std::memory_order_relaxed);
        state.poolExhaustions.store(0, std::memory_order_relaxed);
        std::lock_guard lock(state.samplesMutex);
        state.actualDelaySamples.clear();
        state.schedulingSamples.clear();
        state.actualCursor = 0;
        state.schedulingCursor = 0;
    }
    globalMaximumQueueDepth_.store(
        globalQueueDepth_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    receiverErrors_.store(0, std::memory_order_relaxed);
    senderErrors_.store(0, std::memory_order_relaxed);
    consecutiveSendFailures_.store(0, std::memory_order_relaxed);
    csvRecordsDropped_.store(0, std::memory_order_relaxed);
}

MetricsSnapshot MetricsRegistry::Snapshot() const {
    MetricsSnapshot result{};
    std::vector<double> combinedScheduling;
    combinedScheduling.reserve(sampleCapacity_ * states_.size());
    double totalSchedulingSum = 0.0;
    std::uint64_t totalSchedulingCount = 0;

    for (std::size_t index = 0; index < states_.size(); ++index) {
        const auto& state = states_[index];
        auto& output = result.directions[index];
        output.capturedPackets = state.captured.load(std::memory_order_relaxed);
        output.capturedBytes = state.capturedBytes.load(std::memory_order_relaxed);
        output.scheduledPackets = state.scheduled.load(std::memory_order_relaxed);
        output.scheduledBytes = state.scheduledBytes.load(std::memory_order_relaxed);
        output.injectedPackets = state.injected.load(std::memory_order_relaxed);
        output.injectedBytes = state.injectedBytes.load(std::memory_order_relaxed);
        output.bypassPackets = state.bypass.load(std::memory_order_relaxed);
        output.bypassBytes = state.bypassBytes.load(std::memory_order_relaxed);
        output.droppedPackets = state.dropped.load(std::memory_order_relaxed);
        output.queueDepth = state.queueDepth.load(std::memory_order_relaxed);
        output.maximumQueueDepth = state.maximumQueueDepth.load(std::memory_order_relaxed);
        output.queuePressureEvents = state.queuePressureEvents.load(std::memory_order_relaxed);
        const auto actualCount = state.actualDelayCount.load(std::memory_order_relaxed);
        output.averageActualDelayUs = actualCount == 0 ? 0.0 :
            state.actualDelaySumUs.load(std::memory_order_relaxed) /
                static_cast<double>(actualCount);
        output.maximumActualDelayUs = state.maximumActualDelayUs.load(std::memory_order_relaxed);
        const auto schedulingCount = state.schedulingCount.load(std::memory_order_relaxed);
        const auto schedulingSum = state.schedulingSumUs.load(std::memory_order_relaxed);
        output.averageSchedulingErrorUs = schedulingCount == 0 ? 0.0 :
            schedulingSum / static_cast<double>(schedulingCount);
        output.maximumSchedulingErrorUs =
            state.maximumSchedulingErrorUs.load(std::memory_order_relaxed);
        output.sendFailures = state.sendFailures.load(std::memory_order_relaxed);
        output.poolExhaustions = state.poolExhaustions.load(std::memory_order_relaxed);

        std::vector<double> actualSamples;
        std::vector<double> schedulingSamples;
        {
            std::lock_guard lock(state.samplesMutex);
            actualSamples = state.actualDelaySamples;
            schedulingSamples = state.schedulingSamples;
        }
        std::sort(actualSamples.begin(), actualSamples.end());
        output.p50ActualDelayUs = Percentile(actualSamples, 0.50);
        output.p95ActualDelayUs = Percentile(actualSamples, 0.95);
        output.p99ActualDelayUs = Percentile(actualSamples, 0.99);
        combinedScheduling.insert(
            combinedScheduling.end(), schedulingSamples.begin(), schedulingSamples.end());
        std::sort(schedulingSamples.begin(), schedulingSamples.end());
        output.p50SchedulingErrorUs = Percentile(schedulingSamples, 0.50);
        output.p95SchedulingErrorUs = Percentile(schedulingSamples, 0.95);
        output.p99SchedulingErrorUs = Percentile(schedulingSamples, 0.99);

        result.capturedPackets += output.capturedPackets;
        result.injectedPackets += output.injectedPackets;
        result.droppedPackets += output.droppedPackets;
        totalSchedulingSum += schedulingSum;
        totalSchedulingCount += schedulingCount;
        result.maximumSchedulingErrorUs =
            (std::max)(result.maximumSchedulingErrorUs, output.maximumSchedulingErrorUs);
    }

    result.queueDepth = globalQueueDepth_.load(std::memory_order_relaxed);
    result.maximumQueueDepth = globalMaximumQueueDepth_.load(std::memory_order_relaxed);
    result.currentPoolUsage = result.queueDepth;
    result.maximumPoolUsage = result.maximumQueueDepth;
    result.receiverErrors = receiverErrors_.load(std::memory_order_relaxed);
    result.senderErrors = senderErrors_.load(std::memory_order_relaxed);
    result.consecutiveSendFailures = consecutiveSendFailures_.load(std::memory_order_relaxed);
    result.csvRecordsDropped = csvRecordsDropped_.load(std::memory_order_relaxed);
    result.averageSchedulingErrorUs = totalSchedulingCount == 0 ? 0.0 :
        totalSchedulingSum / static_cast<double>(totalSchedulingCount);
    std::sort(combinedScheduling.begin(), combinedScheduling.end());
    result.p50SchedulingErrorUs = Percentile(combinedScheduling, 0.50);
    result.p95SchedulingErrorUs = Percentile(combinedScheduling, 0.95);
    result.p99SchedulingErrorUs = Percentile(combinedScheduling, 0.99);
    return result;
}

void MetricsRegistry::UpdateMaximum(
    std::atomic<std::uint64_t>& target,
    std::uint64_t value) noexcept {
    auto current = target.load(std::memory_order_relaxed);
    while (current < value && !target.compare_exchange_weak(
        current, value, std::memory_order_relaxed)) {
    }
}

void MetricsRegistry::UpdateMaximum(std::atomic<double>& target, double value) noexcept {
    auto current = target.load(std::memory_order_relaxed);
    while (current < value && !target.compare_exchange_weak(
        current, value, std::memory_order_relaxed)) {
    }
}

void MetricsRegistry::AddBoundedSample(
    std::vector<double>& samples,
    std::size_t& cursor,
    std::size_t capacity,
    double value) {
    if (samples.size() < capacity) {
        samples.push_back(value);
        return;
    }
    samples[cursor] = value;
    cursor = (cursor + 1) % capacity;
}

}  // namespace nlc
