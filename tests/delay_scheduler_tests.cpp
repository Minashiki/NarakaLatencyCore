#include "nlc/delay_scheduler.h"
#include "nlc/qpc_clock.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <span>
#include <thread>
#include <vector>

namespace {

struct RecordedPacket {
    std::uint64_t sequence;
    std::int64_t captureTime;
    std::int64_t dueTime;
    std::int64_t observedSendTime;
    nlc::Direction direction;
    WINDIVERT_ADDRESS address;
    std::vector<std::byte> bytes;
};

class RecordingInjector final {
public:
    bool Inject(std::span<const nlc::PacketView> packets) {
        const auto now = nlc::QpcClock::Now();
        {
            std::lock_guard lock(mutex_);
            for (const auto& packet : packets) {
                packets_.push_back(RecordedPacket{
                    packet.sequence, packet.captureTime, packet.dueTime, now, packet.direction,
                    *packet.address,
                    std::vector<std::byte>(packet.data, packet.data + packet.length)});
            }
        }
        condition_.notify_all();
        return true;
    }

    bool WaitForCount(std::size_t count, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, timeout, [&] { return packets_.size() >= count; });
    }

    std::vector<RecordedPacket> Snapshot() const {
        std::lock_guard lock(mutex_);
        return packets_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<RecordedPacket> packets_;
};

nlc::SchedulerOptions TestOptions(std::size_t capacity = 4'096) {
    nlc::SchedulerOptions options;
    options.poolCapacity = capacity;
    options.bytesPerPacket = 256;
    options.maximumBatchSize = 64;
    options.spinThresholdUs = 200;
    return options;
}

WINDIVERT_ADDRESS Address(nlc::Direction direction) {
    WINDIVERT_ADDRESS address{};
    address.Outbound = direction == nlc::Direction::Outbound ? 1 : 0;
    return address;
}

std::array<std::byte, 16> Payload(std::uint64_t value = 0) {
    std::array<std::byte, 16> payload{};
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        payload[index] = static_cast<std::byte>((value >> (index * 8)) & 0xff);
    }
    return payload;
}

class DelayValueTest : public ::testing::TestWithParam<std::int64_t> {};

TEST_P(DelayValueTest, AppliesConfiguredDelayWithoutEarlyRelease) {
    nlc::DelaySettings settings;
    settings.outboundDelayUs.store(GetParam());
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    const auto payload = Payload();
    const auto expectedResult = GetParam() == 0
        ? nlc::EnqueueResult::InjectedImmediately
        : nlc::EnqueueResult::Scheduled;
    ASSERT_EQ(scheduler.Enqueue(payload, Address(nlc::Direction::Outbound)), expectedResult);
    ASSERT_TRUE(recorder.WaitForCount(1));
    scheduler.StopAndFlush();

    const auto packet = recorder.Snapshot().front();
    const auto actualDelayUs =
        nlc::QpcClock::TicksToMicroseconds(packet.observedSendTime - packet.captureTime);
    EXPECT_GE(actualDelayUs, static_cast<double>(GetParam()) - 100.0);
    EXPECT_GE(packet.observedSendTime, packet.dueTime);
}

INSTANTIATE_TEST_SUITE_P(
    RequiredDelays,
    DelayValueTest,
    ::testing::Values<std::int64_t>(0, 1'000, 5'500, 20'000));

TEST(DelaySchedulerTest, InboundAndOutboundUseDifferentDelays) {
    nlc::DelaySettings settings;
    settings.inboundDelayUs.store(20'000);
    settings.outboundDelayUs.store(2'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    const auto payload = Payload();
    const auto capture = nlc::QpcClock::Now();
    (void)scheduler.Enqueue(payload, Address(nlc::Direction::Inbound), capture);
    (void)scheduler.Enqueue(payload, Address(nlc::Direction::Outbound), capture);
    ASSERT_TRUE(recorder.WaitForCount(2));
    scheduler.StopAndFlush();

    const auto packets = recorder.Snapshot();
    ASSERT_EQ(packets.size(), 2u);
    EXPECT_EQ(packets[0].direction, nlc::Direction::Outbound);
    EXPECT_EQ(packets[1].direction, nlc::Direction::Inbound);
    EXPECT_EQ(
        packets[1].dueTime - packets[0].dueTime,
        nlc::QpcClock::MicrosecondsToTicks(18'000));
}

TEST(DelaySchedulerTest, OnlyInboundDelayCanBeEnabled) {
    nlc::DelaySettings settings;
    settings.inboundEnabled.store(true);
    settings.inboundDelayUs.store(10'000);
    settings.outboundEnabled.store(false);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    const auto payload = Payload();
    (void)scheduler.Enqueue(payload, Address(nlc::Direction::Inbound));
    EXPECT_EQ(scheduler.Enqueue(payload, Address(nlc::Direction::Outbound)),
              nlc::EnqueueResult::InjectedImmediately);
    ASSERT_TRUE(recorder.WaitForCount(2));
    scheduler.StopAndFlush();
    const auto packets = recorder.Snapshot();
    EXPECT_EQ(packets.front().direction, nlc::Direction::Outbound);
}

TEST(DelaySchedulerTest, OnlyOutboundDelayCanBeEnabled) {
    nlc::DelaySettings settings;
    settings.inboundEnabled.store(false);
    settings.outboundEnabled.store(true);
    settings.outboundDelayUs.store(10'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    const auto payload = Payload();
    EXPECT_EQ(scheduler.Enqueue(payload, Address(nlc::Direction::Inbound)),
              nlc::EnqueueResult::InjectedImmediately);
    (void)scheduler.Enqueue(payload, Address(nlc::Direction::Outbound));
    ASSERT_TRUE(recorder.WaitForCount(2));
    scheduler.StopAndFlush();
    EXPECT_EQ(recorder.Snapshot().front().direction, nlc::Direction::Inbound);
}

TEST(DelaySchedulerTest, EqualDueTimesPreserveCaptureOrder) {
    nlc::DelaySettings settings;
    settings.outboundDelayUs.store(30'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    const auto capture = nlc::QpcClock::Now();
    for (std::uint64_t index = 0; index < 200; ++index) {
        const auto payload = Payload(index);
        ASSERT_EQ(scheduler.Enqueue(payload, Address(nlc::Direction::Outbound), capture),
                  nlc::EnqueueResult::Scheduled);
    }
    ASSERT_TRUE(recorder.WaitForCount(200));
    scheduler.StopAndFlush();
    const auto packets = recorder.Snapshot();
    for (std::size_t index = 0; index < packets.size(); ++index) {
        EXPECT_EQ(packets[index].sequence, index);
    }
}

TEST(DelaySchedulerTest, PreservesPacketBytesAndCompleteWinDivertAddress) {
    nlc::DelaySettings settings;
    settings.inboundDelayUs.store(1'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    const auto payload = Payload(0x123456789abcdef0ULL);
    auto address = Address(nlc::Direction::Inbound);
    address.Timestamp = 0x1020304050607080LL;
    address.Loopback = 1;
    address.IPv6 = 1;
    address.Network.IfIdx = 37;
    address.Network.SubIfIdx = 11;
    ASSERT_EQ(scheduler.Enqueue(payload, address), nlc::EnqueueResult::Scheduled);
    ASSERT_TRUE(recorder.WaitForCount(1));
    scheduler.StopAndFlush();

    const auto packet = recorder.Snapshot().front();
    EXPECT_EQ(packet.bytes, std::vector<std::byte>(payload.begin(), payload.end()));
    EXPECT_EQ(std::memcmp(&packet.address, &address, sizeof(address)), 0);
}

TEST(DelaySchedulerTest, SupportsConcurrentProducers) {
    constexpr std::size_t threadCount = 8;
    constexpr std::size_t packetsPerThread = 500;
    nlc::DelaySettings settings;
    settings.outboundDelayUs.store(5'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); },
        TestOptions(threadCount * packetsPerThread + 512));
    scheduler.Start();
    std::vector<std::thread> producers;
    for (std::size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
        producers.emplace_back([&, threadIndex] {
            for (std::size_t index = 0; index < packetsPerThread; ++index) {
                const auto payload = Payload(threadIndex * packetsPerThread + index);
                (void)scheduler.Enqueue(payload, Address(nlc::Direction::Outbound));
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    ASSERT_TRUE(recorder.WaitForCount(threadCount * packetsPerThread));
    scheduler.StopAndFlush();
    const auto metrics = scheduler.Metrics();
    EXPECT_EQ(metrics.capturedPackets, threadCount * packetsPerThread);
    EXPECT_EQ(metrics.injectedPackets, threadCount * packetsPerThread);
    EXPECT_EQ(metrics.droppedPackets, 0u);
}

TEST(DelaySchedulerTest, DelayChangesOnlyAffectNewPackets) {
    nlc::DelaySettings settings;
    settings.outboundDelayUs.store(30'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    const auto payload = Payload();
    const auto capture = nlc::QpcClock::Now();
    (void)scheduler.Enqueue(payload, Address(nlc::Direction::Outbound), capture);
    settings.outboundDelayUs.store(1'000);
    (void)scheduler.Enqueue(payload, Address(nlc::Direction::Outbound), capture);
    ASSERT_TRUE(recorder.WaitForCount(2));
    scheduler.StopAndFlush();
    const auto packets = recorder.Snapshot();
    ASSERT_EQ(packets.size(), 2u);
    EXPECT_EQ(packets[0].sequence, 1u);
    EXPECT_EQ(packets[1].sequence, 0u);
    EXPECT_EQ(packets[1].dueTime - packets[0].dueTime,
              nlc::QpcClock::MicrosecondsToTicks(29'000));
}

TEST(DelaySchedulerTest, ModifyingOneDirectionDoesNotChangeTheOther) {
    nlc::DelaySettings settings;
    settings.inboundDelayUs.store(15'000);
    settings.outboundDelayUs.store(15'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    settings.outboundDelayUs.store(1'000);
    const auto payload = Payload();
    const auto capture = nlc::QpcClock::Now();
    (void)scheduler.Enqueue(payload, Address(nlc::Direction::Inbound), capture);
    (void)scheduler.Enqueue(payload, Address(nlc::Direction::Outbound), capture);
    ASSERT_TRUE(recorder.WaitForCount(2));
    scheduler.StopAndFlush();
    const auto packets = recorder.Snapshot();
    EXPECT_EQ(packets[0].direction, nlc::Direction::Outbound);
    EXPECT_EQ(packets[1].direction, nlc::Direction::Inbound);
}

TEST(DelaySchedulerTest, DisablingDirectionImmediatelyPassesOnlyNewPackets) {
    nlc::DelaySettings settings;
    settings.outboundDelayUs.store(50'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    const auto payload = Payload();
    (void)scheduler.Enqueue(payload, Address(nlc::Direction::Outbound));
    settings.outboundEnabled.store(false);
    EXPECT_EQ(scheduler.Enqueue(payload, Address(nlc::Direction::Outbound)),
              nlc::EnqueueResult::InjectedImmediately);
    ASSERT_TRUE(recorder.WaitForCount(2));
    scheduler.StopAndFlush();
    const auto packets = recorder.Snapshot();
    EXPECT_EQ(packets[0].sequence, 1u);
    EXPECT_EQ(packets[1].sequence, 0u);
}

TEST(DelaySchedulerTest, StopAndFlushInjectsAllQueuedPackets) {
    nlc::DelaySettings settings;
    settings.outboundDelayUs.store(1'000'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    const auto payload = Payload();
    for (int index = 0; index < 100; ++index) {
        (void)scheduler.Enqueue(payload, Address(nlc::Direction::Outbound));
    }
    scheduler.StopAndFlush();
    EXPECT_EQ(recorder.Snapshot().size(), 100u);
    const auto metrics = scheduler.Metrics();
    EXPECT_EQ(metrics.injectedPackets, 100u);
    EXPECT_EQ(metrics.droppedPackets, 0u);
    EXPECT_EQ(metrics.queueDepth, 0u);
}

TEST(DelaySchedulerTest, StopAndDropDropsAllQueuedPackets) {
    nlc::DelaySettings settings;
    settings.outboundDelayUs.store(1'000'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, TestOptions());
    scheduler.Start();
    const auto payload = Payload();
    for (int index = 0; index < 100; ++index) {
        (void)scheduler.Enqueue(payload, Address(nlc::Direction::Outbound));
    }
    scheduler.StopAndDrop();
    EXPECT_TRUE(recorder.Snapshot().empty());
    const auto metrics = scheduler.Metrics();
    EXPECT_EQ(metrics.droppedPackets, 100u);
    EXPECT_EQ(metrics.queueDepth, 0u);
}

TEST(DelaySchedulerTest, SustainedHighRateNeverBulkReleasesEarly) {
    constexpr std::size_t packetCount = 20'000;
    nlc::DelaySettings settings;
    settings.outboundDelayUs.store(20'000);
    RecordingInjector recorder;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); },
        TestOptions(packetCount + 512));
    scheduler.Start();
    const auto payload = Payload();
    for (std::size_t index = 0; index < packetCount; ++index) {
        ASSERT_EQ(scheduler.Enqueue(payload, Address(nlc::Direction::Outbound)),
                  nlc::EnqueueResult::Scheduled);
    }
    ASSERT_TRUE(recorder.WaitForCount(packetCount, std::chrono::seconds(15)));
    scheduler.StopAndFlush();
    const auto packets = recorder.Snapshot();
    ASSERT_EQ(packets.size(), packetCount);
    EXPECT_TRUE(std::all_of(packets.begin(), packets.end(), [](const auto& packet) {
        return packet.observedSendTime >= packet.dueTime;
    }));
    const auto metrics = scheduler.Metrics();
    EXPECT_EQ(metrics.droppedPackets, 0u);
    EXPECT_FALSE(scheduler.IsSafetyBypassActive());
}

TEST(DelaySchedulerTest, PoolExhaustionBypassesNewPacketWithoutEarlyBulkRelease) {
    nlc::DelaySettings settings;
    settings.outboundDelayUs.store(50'000);
    RecordingInjector recorder;
    auto options = TestOptions(4);
    options.pressureThresholdPercent = 75;
    nlc::DelayScheduler scheduler(
        settings, [&](auto packets) { return recorder.Inject(packets); }, options);
    scheduler.Start();
    const auto payload = Payload();
    const auto capture = nlc::QpcClock::Now();
    for (int index = 0; index < 4; ++index) {
        ASSERT_EQ(scheduler.Enqueue(payload, Address(nlc::Direction::Outbound), capture),
                  nlc::EnqueueResult::Scheduled);
    }
    EXPECT_EQ(scheduler.Enqueue(payload, Address(nlc::Direction::Outbound), capture),
              nlc::EnqueueResult::BypassedForSafety);
    ASSERT_TRUE(recorder.WaitForCount(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(recorder.Snapshot().size(), 1u);
    ASSERT_TRUE(recorder.WaitForCount(5));
    scheduler.StopAndFlush();

    const auto packets = recorder.Snapshot();
    ASSERT_EQ(packets.size(), 5u);
    EXPECT_EQ(packets.front().sequence, 4u);
    for (std::size_t index = 1; index < packets.size(); ++index) {
        EXPECT_GE(packets[index].observedSendTime, packets[index].dueTime);
    }
    EXPECT_TRUE(scheduler.IsSafetyBypassActive());
    EXPECT_GT(
        scheduler.Metrics().directions[nlc::DirectionIndex(nlc::Direction::Outbound)]
            .queuePressureEvents,
        0u);
}

}  // namespace
