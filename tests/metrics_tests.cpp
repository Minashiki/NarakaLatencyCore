#include "nlc/metrics.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

namespace {

TEST(MetricsTest, KeepsInboundAndOutboundCountersIndependent) {
    nlc::MetricsRegistry metrics(16);
    metrics.RecordCaptured(nlc::Direction::Inbound, 100);
    metrics.RecordCaptured(nlc::Direction::Outbound, 200);
    metrics.RecordCaptured(nlc::Direction::Outbound, 300);
    const auto snapshot = metrics.Snapshot();
    EXPECT_EQ(snapshot.directions[0].capturedPackets, 1u);
    EXPECT_EQ(snapshot.directions[0].capturedBytes, 100u);
    EXPECT_EQ(snapshot.directions[1].capturedPackets, 2u);
    EXPECT_EQ(snapshot.directions[1].capturedBytes, 500u);
}

TEST(MetricsTest, RecordsScheduledAndBypassBytes) {
    nlc::MetricsRegistry metrics(16);
    metrics.RecordScheduled(nlc::Direction::Inbound, 64);
    metrics.RecordBypass(nlc::Direction::Outbound, 128);
    const auto snapshot = metrics.Snapshot();
    EXPECT_EQ(snapshot.directions[0].scheduledBytes, 64u);
    EXPECT_EQ(snapshot.directions[1].bypassBytes, 128u);
}

TEST(MetricsTest, ComputesActualDelayPercentiles) {
    nlc::MetricsRegistry metrics(16);
    for (int value = 1; value <= 10; ++value)
        metrics.RecordInjected(nlc::Direction::Inbound, 1, value * 100.0);
    const auto direction = metrics.Snapshot().directions[0];
    EXPECT_DOUBLE_EQ(direction.p50ActualDelayUs, 500.0);
    EXPECT_DOUBLE_EQ(direction.p95ActualDelayUs, 1000.0);
    EXPECT_DOUBLE_EQ(direction.maximumActualDelayUs, 1000.0);
}

TEST(MetricsTest, ComputesSchedulingErrorPercentilesPerDirection) {
    nlc::MetricsRegistry metrics(16);
    metrics.RecordSchedulingError(nlc::Direction::Inbound, 10);
    metrics.RecordSchedulingError(nlc::Direction::Inbound, 20);
    metrics.RecordSchedulingError(nlc::Direction::Outbound, 500);
    const auto snapshot = metrics.Snapshot();
    EXPECT_DOUBLE_EQ(snapshot.directions[0].p99SchedulingErrorUs, 20);
    EXPECT_DOUBLE_EQ(snapshot.directions[1].p99SchedulingErrorUs, 500);
}

TEST(MetricsTest, ClampsNegativeSchedulingErrorToZero) {
    nlc::MetricsRegistry metrics(4);
    metrics.RecordSchedulingError(nlc::Direction::Inbound, -25);
    EXPECT_DOUBLE_EQ(metrics.Snapshot().directions[0].maximumSchedulingErrorUs, 0);
}

TEST(MetricsTest, UsesBoundedRecentSampleWindow) {
    nlc::MetricsRegistry metrics(4);
    EXPECT_EQ(metrics.SampleCapacity(), 4u);
    for (int value = 1; value <= 10; ++value)
        metrics.RecordSchedulingError(nlc::Direction::Inbound, value);
    EXPECT_GE(metrics.Snapshot().directions[0].p50SchedulingErrorUs, 7);
}

TEST(MetricsTest, ResetPreservesCurrentQueueDepth) {
    nlc::MetricsRegistry metrics(8);
    metrics.RecordQueuePush(nlc::Direction::Inbound);
    metrics.RecordCaptured(nlc::Direction::Inbound, 10);
    metrics.Reset();
    const auto snapshot = metrics.Snapshot();
    EXPECT_EQ(snapshot.queueDepth, 1u);
    EXPECT_EQ(snapshot.capturedPackets, 0u);
    metrics.RecordQueuePop(nlc::Direction::Inbound);
}

TEST(MetricsTest, TracksFailuresAndPoolExhaustion) {
    nlc::MetricsRegistry metrics(8);
    metrics.RecordSendFailure(nlc::Direction::Outbound);
    metrics.RecordPoolExhaustion(nlc::Direction::Outbound);
    metrics.RecordReceiverError();
    metrics.RecordSenderError();
    metrics.SetConsecutiveSendFailures(3);
    const auto snapshot = metrics.Snapshot();
    EXPECT_EQ(snapshot.directions[1].sendFailures, 1u);
    EXPECT_EQ(snapshot.directions[1].poolExhaustions, 1u);
    EXPECT_EQ(snapshot.receiverErrors, 1u);
    EXPECT_EQ(snapshot.senderErrors, 1u);
    EXPECT_EQ(snapshot.consecutiveSendFailures, 3u);
}

TEST(MetricsTest, ConcurrentSnapshotsRemainConsistentAndDoNotCrash) {
    nlc::MetricsRegistry metrics(128);
    std::atomic<bool> running{true};
    std::thread writer([&] {
        for (int index = 0; index < 10'000; ++index) {
            metrics.RecordCaptured(nlc::Direction::Inbound, 64);
            metrics.RecordSchedulingError(nlc::Direction::Inbound, index % 100);
        }
        running.store(false);
    });
    while (running.load()) {
        const auto snapshot = metrics.Snapshot();
        EXPECT_GE(snapshot.directions[0].capturedBytes,
                  snapshot.directions[0].capturedPackets);
    }
    writer.join();
    EXPECT_EQ(metrics.Snapshot().capturedPackets, 10'000u);
}

TEST(MetricsTest, GlobalMaximumQueueDepthTracksSimultaneousDepth) {
    nlc::MetricsRegistry metrics(8);
    metrics.RecordQueuePush(nlc::Direction::Inbound);
    metrics.RecordQueuePush(nlc::Direction::Outbound);
    metrics.RecordQueuePop(nlc::Direction::Inbound);
    const auto snapshot = metrics.Snapshot();
    EXPECT_EQ(snapshot.queueDepth, 1u);
    EXPECT_EQ(snapshot.maximumQueueDepth, 2u);
    metrics.RecordQueuePop(nlc::Direction::Outbound);
}

}  // namespace

