#pragma once

#include "nlc/delay_settings.h"
#include "nlc/async_csv_recorder.h"
#include "nlc/metrics.h"
#include "nlc/packet_pool.h"

#include <windivert.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <queue>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace nlc {

struct PacketView {
    const std::byte* data{nullptr};
    std::uint32_t length{0};
    const WINDIVERT_ADDRESS* address{nullptr};
    std::uint64_t sequence{0};
    std::int64_t captureTime{0};
    std::int64_t dueTime{0};
    Direction direction{Direction::Inbound};
};

using InjectBatch = std::function<bool(std::span<const PacketView>)>;

struct SchedulerOptions {
    std::size_t poolCapacity{2'048};
    std::size_t bytesPerPacket{65'535};
    std::size_t maximumBatchSize{64};
    std::size_t pressureThresholdPercent{90};
    std::size_t consecutiveSendFailureThreshold{3};
    std::int64_t spinThresholdUs{200};
    std::string csvPath{};
};

enum class EnqueueResult {
    Scheduled,
    InjectedImmediately,
    BypassedForSafety,
    Failed
};

class DelayScheduler final {
public:
    DelayScheduler(DelaySettings& settings, InjectBatch injectBatch, SchedulerOptions options = {});
    ~DelayScheduler();

    DelayScheduler(const DelayScheduler&) = delete;
    DelayScheduler& operator=(const DelayScheduler&) = delete;

    void Start();
    [[nodiscard]] EnqueueResult Enqueue(
        std::span<const std::byte> bytes,
        const WINDIVERT_ADDRESS& address,
        std::int64_t captureTime = 0);

    void StopAndFlush();
    void StopAndDrop();

    [[nodiscard]] MetricsSnapshot Metrics() const;
    void ResetMetrics() noexcept;
    void RecordReceiverError() noexcept;
    void ActivateSafetyBypass() noexcept;
    [[nodiscard]] bool IsSafetyBypassActive() const noexcept;

private:
    enum class StopMode { None, Flush, Drop };

    struct QueueItem {
        std::int64_t dueTime{0};
        std::uint64_t sequence{0};
        std::size_t poolIndex{0};
    };

    struct LaterDueTime {
        bool operator()(const QueueItem& left, const QueueItem& right) const noexcept {
            if (left.dueTime != right.dueTime) {
                return left.dueTime > right.dueTime;
            }
            return left.sequence > right.sequence;
        }
    };

    EnqueueResult InjectImmediate(
        std::span<const std::byte> bytes,
        const WINDIVERT_ADDRESS& address,
        Direction direction,
        std::int64_t captureTime,
        std::uint64_t sequence,
        EnqueueResult successResult);
    void SenderMain();
    void WaitUntil(std::int64_t dueTime);
    void DrainDuePackets(bool flushAll);
    void DropAllQueued();
    void FinishStop(StopMode mode);
    void WriteCsv(
        const PacketView& packet,
        std::int64_t actualSendTime,
        double actualDelayUs,
        double errorUs,
        bool sendResult);
    void InitializeWaitHandles();
    void CloseWaitHandles() noexcept;

    DelaySettings& settings_;
    InjectBatch injectBatch_;
    SchedulerOptions options_;
    PacketPool pool_;
    MetricsRegistry metrics_;

    mutable std::mutex queueMutex_;
    std::priority_queue<QueueItem, std::vector<QueueItem>, LaterDueTime> queue_;
    std::vector<QueueItem> batchItems_;
    std::vector<PacketView> batchViews_;
    std::atomic<std::uint64_t> nextSequence_{0};
    std::atomic<StopMode> stopMode_{StopMode::None};
    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
    std::atomic<bool> safetyBypass_{false};
    std::atomic<std::uint64_t> consecutiveSendFailures_{0};
    std::thread senderThread_;

    void* timerHandle_{nullptr};
    void* wakeEventHandle_{nullptr};
    std::unique_ptr<AsyncCsvRecorder> csv_;
};

}  // namespace nlc
