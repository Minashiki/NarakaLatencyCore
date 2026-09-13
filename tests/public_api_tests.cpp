#include "nlc/public_api.h"

#include <gtest/gtest.h>

#include <cstddef>

namespace {

class PublicApiTest : public ::testing::Test {
protected:
    void TearDown() override { nl_shutdown(); }
};

TEST_F(PublicApiTest, InitializeIsIdempotent) {
    EXPECT_EQ(nl_initialize(), NL_OK);
    EXPECT_EQ(nl_initialize(), NL_OK);
}

TEST_F(PublicApiTest, ShutdownIsIdempotent) {
    ASSERT_EQ(nl_initialize(), NL_OK);
    nl_shutdown();
    EXPECT_NO_THROW(nl_shutdown());
}

TEST_F(PublicApiTest, NullSettingsReturnInvalidArgument) {
    ASSERT_EQ(nl_initialize(), NL_OK);
    EXPECT_EQ(nl_start(nullptr), NL_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(nl_update_settings(nullptr), NL_ERROR_INVALID_ARGUMENT);
}

TEST_F(PublicApiTest, SmallSettingsStructReturnsVersionMismatch) {
    ASSERT_EQ(nl_initialize(), NL_OK);
    NL_DelaySettings settings{};
    settings.structSize = 4;
    EXPECT_EQ(nl_start(&settings), NL_ERROR_VERSION_MISMATCH);
}

TEST_F(PublicApiTest, NullStateAndMetricsPointersReturnInvalidArgument) {
    ASSERT_EQ(nl_initialize(), NL_OK);
    EXPECT_EQ(nl_get_state(nullptr), NL_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(nl_get_metrics(nullptr), NL_ERROR_INVALID_ARGUMENT);
}

TEST_F(PublicApiTest, SmallMetricsStructReturnsVersionMismatch) {
    ASSERT_EQ(nl_initialize(), NL_OK);
    NL_MetricsSnapshot metrics{};
    metrics.structSize = 1;
    EXPECT_EQ(nl_get_metrics(&metrics), NL_ERROR_VERSION_MISMATCH);
}

TEST_F(PublicApiTest, TooSmallErrorBufferIsReported) {
    ASSERT_EQ(nl_initialize(), NL_OK);
    NL_DelaySettings settings{};
    settings.structSize = sizeof(settings);
    settings.inboundEnabled = 1;
    settings.inboundDelayUs = -1;
    EXPECT_EQ(nl_start(&settings), NL_ERROR_INVALID_ARGUMENT);
    wchar_t buffer[2]{};
    EXPECT_EQ(nl_get_last_error(buffer, 2), NL_ERROR_BUFFER_TOO_SMALL);
}

TEST_F(PublicApiTest, FixedAbiLayoutsRemainStable) {
    EXPECT_EQ(sizeof(NL_DelaySettings), 56u);
    EXPECT_EQ(offsetof(NL_DelaySettings, inboundDelayUs), 8u);
    EXPECT_EQ(offsetof(NL_DelaySettings, outboundDelayUs), 16u);
    EXPECT_EQ(sizeof(NL_DirectionMetrics), 232u);
    EXPECT_EQ(sizeof(NL_MetricsSnapshot), 1928u);
}

TEST_F(PublicApiTest, GetStoppedMetricsReturnsSnapshotNotPointer) {
    ASSERT_EQ(nl_initialize(), NL_OK);
    NL_MetricsSnapshot metrics{};
    metrics.structSize = sizeof(metrics);
    ASSERT_EQ(nl_get_metrics(&metrics), NL_OK);
    EXPECT_EQ(metrics.engineState, NL_STATE_STOPPED);
    EXPECT_STREQ(metrics.activeFilter,
                 L"!loopback and !impostor and !fragment and (tcp or udp)");
}

}  // namespace

