#include "nlc/delay_scheduler.h"

#include "nlc/qpc_clock.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <system_error>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace nlc {

DelayScheduler::DelayScheduler(
    DelaySettings& settings,
    InjectBatch injectBatch,
    SchedulerOptions options)
    : settings_(settings),
      injectBatch_(std::move(injectBatch)),
      options_(std::move(options)),
      pool_(options_.poolCapacity, options_.bytesPerPacket),
      metrics_() {
    if (!injectBatch_) {
        throw std::invalid_argument("inject callback is required");
    }
    if (options_.maximumBatchSize == 0 || options_.pressureThresholdPercent > 100 ||
        options_.spinThresholdUs < 0 || options_.consecutiveSendFailureThreshold == 0) {
        throw std::invalid_argument("invalid scheduler options");
    }
    std::vector<QueueItem> queueStorage;
    queueStorage.reserve(options_.poolCapacity);
    queue_ = decltype(queue_)(LaterDueTime{}, std::move(queueStorage));
    batchItems_.reserve(options_.maximumBatchSize);
    batchViews_.reserve(options_.maximumBatchSize);
    if (!options_.csvPath.empty()) {
        csv_ = std::make_unique<AsyncCsvRecorder>(options_.csvPath);
    }
}

DelayScheduler::~DelayScheduler() {
    StopAndFlush();
    CloseWaitHandles();
}

void DelayScheduler::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    InitializeWaitHandles();
    senderThread_ = std::thread(&DelayScheduler::SenderMain, this);
}

EnqueueResult DelayScheduler::Enqueue(
    std::span<const std::byte> bytes,
    const WINDIVERT_ADDRESS& address,
    std::int64_t captureTime) {
    const auto direction = address.Outbound ? Direction::Outbound : Direction::Inbound;
    metrics_.RecordCaptured(direction, bytes.size());
    const auto sequence = nextSequence_.fetch_add(1, std::memory_order_relaxed);
    if (captureTime == 0) {
        captureTime = QpcClock::Now();
    }

    if (!started_.load(std::memory_order_acquire) ||
        stopMode_.load(std::memory_order_acquire) != StopMode::None) {
        metrics_.RecordDropped(direction);
        return EnqueueResult::Failed;
    }

    const auto configuredDelayUs = settings_.DelayUs(direction);
    if (!settings_.Enabled(direction) || configuredDelayUs == 0) {
        return InjectImmediate(
            bytes, address, direction, captureTime, sequence,
            EnqueueResult::InjectedImmediately);
    }

    if (safetyBypass_.load(std::memory_order_acquire)) {
        return InjectImmediate(
            bytes, address, direction, captureTime, sequence,
            EnqueueResult::BypassedForSafety);
    }

    if (bytes.size() > pool_.BytesPerPacket()) {
        safetyBypass_.store(true, std::memory_order_release);
        metrics_.RecordQueuePressure(direction);
        metrics_.RecordPoolExhaustion(direction);
        return InjectImmediate(
            bytes, address, direction, captureTime, sequence,
            EnqueueResult::BypassedForSafety);
    }

    const auto acquired = pool_.Acquire();
    if (!acquired) {
        safetyBypass_.store(true, std::memory_order_release);
        metrics_.RecordQueuePressure(direction);
        metrics_.RecordPoolExhaustion(direction);
        return InjectImmediate(
            bytes, address, direction, captureTime, sequence,
            EnqueueResult::BypassedForSafety);
    }

    const auto poolIndex = *acquired;
    auto& packet = pool_.Packet(poolIndex);
    packet.address = address;
    packet.length = static_cast<std::uint32_t>(bytes.size());
    packet.sequence = sequence;
    packet.captureTime = captureTime;
    packet.dueTime = captureTime + QpcClock::MicrosecondsToTicks(configuredDelayUs);
    packet.direction = direction;
    std::memcpy(pool_.WritableBytes(poolIndex).data(), bytes.data(), bytes.size());

    const auto used = pool_.Capacity() - pool_.Available();
    if (used * 100 >= pool_.Capacity() * options_.pressureThresholdPercent) {
        metrics_.RecordQueuePressure(direction);
    }

    {
        std::lock_guard lock(queueMutex_);
        if (stopMode_.load(std::memory_order_acquire) != StopMode::None) {
            pool_.Release(poolIndex);
            metrics_.RecordDropped(direction);
            return EnqueueResult::Failed;
        }
        queue_.push(QueueItem{packet.dueTime, packet.sequence, poolIndex});
        metrics_.RecordQueuePush(direction);
        metrics_.RecordScheduled(direction, bytes.size());
    }
    SetEvent(static_cast<HANDLE>(wakeEventHandle_));
    return EnqueueResult::Scheduled;
}

void DelayScheduler::StopAndFlush() {
    StopMode expected = StopMode::None;
    stopMode_.compare_exchange_strong(expected, StopMode::Flush, std::memory_order_acq_rel);
    if (wakeEventHandle_ != nullptr) {
        SetEvent(static_cast<HANDLE>(wakeEventHandle_));
    }
    if (senderThread_.joinable() && senderThread_.get_id() != std::this_thread::get_id()) {
        senderThread_.join();
    }
    if (!started_.load(std::memory_order_acquire)) {
        stopped_.store(true, std::memory_order_release);
    }
}

void DelayScheduler::StopAndDrop() {
    StopMode expected = StopMode::None;
    stopMode_.compare_exchange_strong(expected, StopMode::Drop, std::memory_order_acq_rel);
    if (wakeEventHandle_ != nullptr) {
        SetEvent(static_cast<HANDLE>(wakeEventHandle_));
    }
    if (senderThread_.joinable() && senderThread_.get_id() != std::this_thread::get_id()) {
        senderThread_.join();
    }
    if (!started_.load(std::memory_order_acquire)) {
        stopped_.store(true, std::memory_order_release);
    }
}

MetricsSnapshot DelayScheduler::Metrics() const {
    auto snapshot = metrics_.Snapshot();
    for (const auto direction : {Direction::Inbound, Direction::Outbound}) {
        auto& item = snapshot.directions[DirectionIndex(direction)];
        item.enabled = settings_.Enabled(direction);
        item.configuredDelayUs = settings_.DelayUs(direction);
    }
    return snapshot;
}

void DelayScheduler::ResetMetrics() noexcept {
    metrics_.Reset();
}

void DelayScheduler::RecordReceiverError() noexcept {
    metrics_.RecordReceiverError();
}

void DelayScheduler::ActivateSafetyBypass() noexcept {
    safetyBypass_.store(true, std::memory_order_release);
}

bool DelayScheduler::IsSafetyBypassActive() const noexcept {
    return safetyBypass_.load(std::memory_order_acquire);
}

EnqueueResult DelayScheduler::InjectImmediate(
    std::span<const std::byte> bytes,
    const WINDIVERT_ADDRESS& address,
    Direction direction,
    std::int64_t captureTime,
    std::uint64_t sequence,
    EnqueueResult successResult) {
    const PacketView packet{
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), &address, sequence,
        captureTime, captureTime, direction};
    metrics_.RecordBypass(direction, bytes.size());
    bool success = false;
    try {
        success = injectBatch_(std::span<const PacketView>(&packet, 1));
    } catch (...) {
        success = false;
    }
    if (success) {
        metrics_.RecordInjected(
            direction, bytes.size(),
            QpcClock::TicksToMicroseconds(QpcClock::Now() - captureTime));
        consecutiveSendFailures_.store(0, std::memory_order_relaxed);
        metrics_.SetConsecutiveSendFailures(0);
        return successResult;
    }
    metrics_.RecordSendFailure(direction);
    const auto failures = consecutiveSendFailures_.fetch_add(1, std::memory_order_relaxed) + 1;
    metrics_.SetConsecutiveSendFailures(failures);
    if (failures >= options_.consecutiveSendFailureThreshold) {
        safetyBypass_.store(true, std::memory_order_release);
    }
    metrics_.RecordDropped(direction);
    return EnqueueResult::Failed;
}

void DelayScheduler::SenderMain() {
    for (;;) {
        const auto stopMode = stopMode_.load(std::memory_order_acquire);
        if (stopMode != StopMode::None) {
            FinishStop(stopMode);
            return;
        }

        std::int64_t dueTime = 0;
        {
            std::lock_guard lock(queueMutex_);
            if (!queue_.empty()) {
                dueTime = queue_.top().dueTime;
            }
        }
        if (dueTime == 0) {
            WaitForSingleObject(static_cast<HANDLE>(wakeEventHandle_), INFINITE);
            continue;
        }
        if (QpcClock::Now() < dueTime) {
            WaitUntil(dueTime);
            continue;
        }
        DrainDuePackets(false);
    }
}

void DelayScheduler::WaitUntil(std::int64_t dueTime) {
    const auto spinTicks = QpcClock::MicrosecondsToTicks(options_.spinThresholdUs);
    for (;;) {
        if (stopMode_.load(std::memory_order_acquire) != StopMode::None) {
            return;
        }
        const auto now = QpcClock::Now();
        const auto remaining = dueTime - now;
        if (remaining <= 0) {
            return;
        }
        if (remaining <= spinTicks) {
            while (QpcClock::Now() < dueTime) {
                if (stopMode_.load(std::memory_order_relaxed) != StopMode::None ||
                    WaitForSingleObject(static_cast<HANDLE>(wakeEventHandle_), 0) == WAIT_OBJECT_0) {
                    return;
                }
                YieldProcessor();
            }
            return;
        }

        const auto waitTicks = remaining - spinTicks;
        const auto waitUs = (std::max<std::int64_t>)(
            1, static_cast<std::int64_t>(QpcClock::TicksToMicroseconds(waitTicks)));
        LARGE_INTEGER relativeDue{};
        relativeDue.QuadPart = -waitUs * 10;
        if (!SetWaitableTimerEx(
                static_cast<HANDLE>(timerHandle_), &relativeDue, 0, nullptr, nullptr, nullptr, 0)) {
            const auto timeoutMs = static_cast<DWORD>((waitUs + 999) / 1'000);
            WaitForSingleObject(static_cast<HANDLE>(wakeEventHandle_), timeoutMs);
            return;
        }
        HANDLE handles[] = {
            static_cast<HANDLE>(timerHandle_), static_cast<HANDLE>(wakeEventHandle_)};
        const auto result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0 + 1) {
            CancelWaitableTimer(static_cast<HANDLE>(timerHandle_));
            return;
        }
    }
}

void DelayScheduler::DrainDuePackets(bool flushAll) {
    for (;;) {
        batchItems_.clear();
        batchViews_.clear();
        {
            std::lock_guard lock(queueMutex_);
            const auto now = QpcClock::Now();
            while (!queue_.empty() && batchItems_.size() < options_.maximumBatchSize &&
                   (flushAll || queue_.top().dueTime <= now)) {
                const auto item = queue_.top();
                queue_.pop();
                metrics_.RecordQueuePop(pool_.Packet(item.poolIndex).direction);
                batchItems_.push_back(item);
            }
        }
        if (batchItems_.empty()) {
            return;
        }

        for (const auto& item : batchItems_) {
            const auto& packet = pool_.Packet(item.poolIndex);
            const auto bytes = pool_.Bytes(item.poolIndex);
            batchViews_.push_back(PacketView{
                bytes.data(), packet.length, &packet.address, packet.sequence,
                packet.captureTime, packet.dueTime, packet.direction});
        }

        const auto actualSendTime = QpcClock::Now();
        bool success = false;
        try {
            success = injectBatch_(batchViews_);
        } catch (...) {
            success = false;
        }

        for (std::size_t index = 0; index < batchItems_.size(); ++index) {
            const auto& view = batchViews_[index];
            const auto actualDelayUs =
                QpcClock::TicksToMicroseconds(actualSendTime - view.captureTime);
            const auto errorUs = QpcClock::TicksToMicroseconds(actualSendTime - view.dueTime);
            if (success) {
                metrics_.RecordInjected(view.direction, view.length, actualDelayUs);
                if (!flushAll) {
                    metrics_.RecordSchedulingError(view.direction, errorUs);
                }
            } else {
                metrics_.RecordSendFailure(view.direction);
                metrics_.RecordDropped(view.direction);
            }
            WriteCsv(view, actualSendTime, actualDelayUs, errorUs, success);
            pool_.Release(batchItems_[index].poolIndex);
        }
        if (success) {
            consecutiveSendFailures_.store(0, std::memory_order_relaxed);
            metrics_.SetConsecutiveSendFailures(0);
        } else {
            const auto failures =
                consecutiveSendFailures_.fetch_add(1, std::memory_order_relaxed) + 1;
            metrics_.SetConsecutiveSendFailures(failures);
            if (failures >= options_.consecutiveSendFailureThreshold) {
                safetyBypass_.store(true, std::memory_order_release);
            }
        }
        if (!flushAll) {
            return;
        }
    }
}

void DelayScheduler::DropAllQueued() {
    for (;;) {
        QueueItem item{};
        {
            std::lock_guard lock(queueMutex_);
            if (queue_.empty()) {
                return;
            }
            item = queue_.top();
            queue_.pop();
        }
        const auto direction = pool_.Packet(item.poolIndex).direction;
        metrics_.RecordQueuePop(direction);
        metrics_.RecordDropped(direction);
        pool_.Release(item.poolIndex);
    }
}

void DelayScheduler::FinishStop(StopMode mode) {
    if (mode == StopMode::Flush) {
        DrainDuePackets(true);
    } else {
        DropAllQueued();
    }
    stopped_.store(true, std::memory_order_release);
}

void DelayScheduler::WriteCsv(
    const PacketView& packet,
    std::int64_t actualSendTime,
    double actualDelayUs,
    double errorUs,
    bool sendResult) {
    if (!csv_) {
        return;
    }
    const CsvSchedulingRecord record{
        packet.sequence,
        packet.direction,
        packet.captureTime,
        packet.dueTime,
        actualSendTime,
        static_cast<std::int64_t>(
            QpcClock::TicksToMicroseconds(packet.dueTime - packet.captureTime)),
        actualDelayUs,
        errorUs,
        sendResult};
    if (!csv_->TryRecord(record)) {
        metrics_.RecordCsvDropped();
    }
}

void DelayScheduler::InitializeWaitHandles() {
    auto timer = CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer == nullptr) {
        timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }
    if (timer == nullptr) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "CreateWaitableTimerExW failed");
    }
    const auto wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (wake == nullptr) {
        const auto error = GetLastError();
        CloseHandle(timer);
        throw std::system_error(
            static_cast<int>(error), std::system_category(), "CreateEventW failed");
    }
    timerHandle_ = timer;
    wakeEventHandle_ = wake;
}

void DelayScheduler::CloseWaitHandles() noexcept {
    if (timerHandle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(timerHandle_));
        timerHandle_ = nullptr;
    }
    if (wakeEventHandle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(wakeEventHandle_));
        wakeEventHandle_ = nullptr;
    }
}

}  // namespace nlc
