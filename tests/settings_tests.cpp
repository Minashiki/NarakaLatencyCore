#include "nlc/delay_settings.h"

#include <gtest/gtest.h>

namespace {

nlc::GlobalDelaySettings ValidSettings() {
    return {true, true, true, 7'000, 3'000};
}

TEST(SettingsTest, AcceptsIndependentSevenAndThreeMillisecondDelays) {
    EXPECT_TRUE(nlc::ValidateSettings(ValidSettings()));
}

TEST(SettingsTest, RejectsNegativeInboundDelay) {
    auto settings = ValidSettings();
    settings.inboundDelayUs = -1;
    EXPECT_FALSE(nlc::ValidateSettings(settings));
}

TEST(SettingsTest, RejectsNegativeOutboundDelay) {
    auto settings = ValidSettings();
    settings.outboundDelayUs = -1;
    EXPECT_FALSE(nlc::ValidateSettings(settings));
}

TEST(SettingsTest, RejectsInboundDelayAboveOneHundredMilliseconds) {
    auto settings = ValidSettings();
    settings.inboundDelayUs = 100'001;
    EXPECT_FALSE(nlc::ValidateSettings(settings));
}

TEST(SettingsTest, RejectsOutboundDelayAboveOneHundredMilliseconds) {
    auto settings = ValidSettings();
    settings.outboundDelayUs = 100'001;
    EXPECT_FALSE(nlc::ValidateSettings(settings));
}

TEST(SettingsTest, AllowsBoundaryValues) {
    auto settings = ValidSettings();
    settings.inboundDelayUs = 0;
    settings.outboundDelayUs = 100'000;
    EXPECT_TRUE(nlc::ValidateSettings(settings));
}

TEST(SettingsTest, RejectsEnabledEngineWithNoEffectiveDirection) {
    auto settings = ValidSettings();
    settings.inboundEnabled = false;
    settings.outboundEnabled = true;
    settings.outboundDelayUs = 0;
    EXPECT_FALSE(nlc::ValidateSettings(settings));
}

TEST(SettingsTest, DisabledEngineAllowsZeroConfiguration) {
    nlc::GlobalDelaySettings settings{};
    EXPECT_TRUE(nlc::ValidateSettings(settings));
}

TEST(SettingsTest, ConvertsFivePointFiveMillisecondsExactly) {
    std::int64_t microseconds = 0;
    ASSERT_TRUE(nlc::TryMillisecondsToMicroseconds(5.5, microseconds));
    EXPECT_EQ(microseconds, 5'500);
}

TEST(SettingsTest, RejectsInvalidMillisecondConversions) {
    std::int64_t microseconds = 0;
    EXPECT_FALSE(nlc::TryMillisecondsToMicroseconds(-0.1, microseconds));
    EXPECT_FALSE(nlc::TryMillisecondsToMicroseconds(100.1, microseconds));
}

TEST(SettingsTest, AtomicStoreAndLoadPreservesDirections) {
    nlc::DelaySettings atomic;
    const auto expected = ValidSettings();
    atomic.Store(expected);
    const auto actual = atomic.Load();
    EXPECT_EQ(actual.inboundEnabled, expected.inboundEnabled);
    EXPECT_EQ(actual.outboundEnabled, expected.outboundEnabled);
    EXPECT_EQ(actual.inboundDelayUs, 7'000);
    EXPECT_EQ(actual.outboundDelayUs, 3'000);
}

TEST(SettingsTest, InboundMutationDoesNotChangeOutbound) {
    nlc::DelaySettings atomic;
    atomic.Store(ValidSettings());
    atomic.inboundDelayUs.store(12'000);
    EXPECT_EQ(atomic.outboundDelayUs.load(), 3'000);
}

TEST(SettingsTest, OutboundMutationDoesNotChangeInbound) {
    nlc::DelaySettings atomic;
    atomic.Store(ValidSettings());
    atomic.outboundEnabled.store(false);
    EXPECT_TRUE(atomic.inboundEnabled.load());
    EXPECT_EQ(atomic.inboundDelayUs.load(), 7'000);
}

}  // namespace

