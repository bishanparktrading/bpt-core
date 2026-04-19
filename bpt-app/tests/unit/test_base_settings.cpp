// Unit tests for bpt::app::load_base_settings.
//
// The run() template itself isn't unit-tested here — it requires a live
// Aeron MediaDriver and TSC calibration, which is awkward in a pure unit
// test. That path gets exercised implicitly when services start using it
// (Phase 3 service migrations).

#include "bpt_app/base_settings.h"

#include <gtest/gtest.h>
#include <toml++/toml.hpp>

namespace {

using bpt::app::BaseSettings;
using bpt::app::load_base_settings;

toml::table parse(const char* src) {
    return toml::parse(src);
}

TEST(BaseSettingsLoader, DefaultsWhenAllBlocksMissing) {
    BaseSettings base;
    auto root = parse("");
    load_base_settings(root, base);
    EXPECT_EQ(base.environment, "");
    EXPECT_EQ(base.media_driver_dir, "");
    EXPECT_EQ(base.metrics_port, 0);
    EXPECT_TRUE(base.calibrate_tsc) << "calibrate_tsc default must survive when TOML has no hook";
}

TEST(BaseSettingsLoader, ReadsTopLevelEnvironment) {
    BaseSettings base;
    auto root = parse(R"(environment = "prod")");
    load_base_settings(root, base);
    EXPECT_EQ(base.environment, "prod");
}

TEST(BaseSettingsLoader, ReadsAeronMediaDriverDir) {
    BaseSettings base;
    auto root = parse(R"(
        [aeron]
        media_driver_dir = "/dev/shm/aeron-bifrost"
    )");
    load_base_settings(root, base);
    EXPECT_EQ(base.media_driver_dir, "/dev/shm/aeron-bifrost");
}

TEST(BaseSettingsLoader, ReadsMetricsPort) {
    BaseSettings base;
    auto root = parse(R"(
        [metrics]
        port = 9103
    )");
    load_base_settings(root, base);
    EXPECT_EQ(base.metrics_port, 9103);
}

TEST(BaseSettingsLoader, ReadsLoggingBlock) {
    // Full delegation to bpt::common::logging::from_toml — this test just
    // verifies the [logging] table reaches it (at least one field takes
    // effect). Exhaustive logging field parsing lives in bpt-common's
    // own tests.
    BaseSettings base;
    auto root = parse(R"(
        [logging]
        level = "debug"
        dir   = "/var/log/myservice"
    )");
    load_base_settings(root, base);
    EXPECT_EQ(base.logging.level, "debug");
    EXPECT_EQ(base.logging.log_dir, "/var/log/myservice");
}

TEST(BaseSettingsLoader, AllTogether) {
    BaseSettings base;
    auto root = parse(R"(
        environment = "qa"
        [aeron]
        media_driver_dir = "/tmp/aeron"
        [logging]
        level = "info"
        [metrics]
        port = 9999
    )");
    load_base_settings(root, base);
    EXPECT_EQ(base.environment, "qa");
    EXPECT_EQ(base.media_driver_dir, "/tmp/aeron");
    EXPECT_EQ(base.logging.level, "info");
    EXPECT_EQ(base.metrics_port, 9999);
}

TEST(BaseSettingsLoader, DoesNotClobberUnspecifiedFields) {
    // Loader is additive: any field not present in the TOML must leave the
    // BaseSettings field at whatever value was passed in. Matters because
    // services set calibrate_tsc = false for backtester, and we must not
    // overwrite that with a default-true when the TOML is silent on it.
    BaseSettings base;
    base.calibrate_tsc = false;
    base.environment = "preset";
    auto root = parse("");  // empty
    load_base_settings(root, base);
    EXPECT_FALSE(base.calibrate_tsc);
    EXPECT_EQ(base.environment, "preset");
}

}  // namespace
