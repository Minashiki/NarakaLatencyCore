#include "nlc/delay_scheduler.h"
#include "nlc/qpc_clock.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace {

TEST(StressTest, OneHundredThousandPacketsNeverReleaseBeforeDueTime) {
    constexpr std::size_t packetCount = 100'000;
    nlc::DelaySettings settings;
    settings.outboundDelayUs.store(5'000);
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<std::size_t> injected{0};
    std::atomic<std::size_t> early{0};
    nlc::SchedulerOptions options;
    options.poolCapacity = packetCount + 256;
    options.bytesPerPacket = 32;
    options.maximumBatchSize = 128;
    nlc::DelayScheduler scheduler(settings, [&](std::span<const nlc::PacketView> packets) {
        const auto now = nlc::QpcClock::Now();
        for (const auto& packet : packets) {
            if (now < packet.dueTime) early.fetch_add(1);
        }
        injected.fetch_add(packets.size());
        condition.notify_all();
        return true;
    }, options);
    scheduler.Start();
    WINDIVERT_ADDRESS address{};
    address.Outbound = 1;
    const std::array<std::byte, 16> payload{};
    for (std::size_t index = 0; index < packetCount; ++index) {
        ASSERT_EQ(scheduler.Enqueue(payload, address), nlc::EnqueueResult::Scheduled);
    }
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(20), [&] {
            return injected.load() == packetCount;
        }));
    }
    scheduler.StopAndFlush();
    EXPECT_EQ(injected.load(), packetCount);
    EXPECT_EQ(early.load(), 0u);
    EXPECT_EQ(scheduler.Metrics().droppedPackets, 0u);
    EXPECT_FALSE(scheduler.IsSafetyBypassActive());
}

}  // namespace
