// Unit tests for heimdall::config::load() — no network, no Aeron.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "heimdall/config/settings.h"

namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────────────────────

static fs::path write_toml(const std::string& content) {
    auto path = fs::temp_directory_path() / "heimdall_test_config.toml";
    std::ofstream f(path);
    f << content;
    return path;
}

// ── Full config ───────────────────────────────────────────────────────────────

TEST(HeimdallConfigTest, ParsesFullConfig) {
    auto path = write_toml(R"(
environment = "qa"
exchanges   = ["OKX"]

[aeron]
media_driver_dir = "/tmp/aeron"

[aeron.order]
channel   = "aeron:ipc"
stream_id = 3001

[aeron.exec_report]
channel   = "aeron:ipc"
stream_id = 3002

[aeron.heartbeat]
channel   = "aeron:ipc"
stream_id = 3003

[heimdall]
heartbeat_interval_ms  = 2000
stale_order_timeout_ms = 60000

[heimdall.risk]
trading_enabled            = false
max_order_size_usd         = 2500.0
max_notional_per_order_usd = 10000.0
max_open_orders_per_venue  = 20
max_orders_per_second      = 5

[[adapters]]
exchange = "OKX"
testnet  = true
ws_host  = "wseeapap.okx.com"
ws_port  = "8443"
ws_path  = "/ws/v5/private"
use_tls  = true

[metrics]
port = 9103
)");

    auto s = heimdall::config::load(path.string());

    EXPECT_EQ(s.environment, "qa");
    ASSERT_EQ(s.exchanges.size(), 1u);
    EXPECT_EQ(s.exchanges[0], "OKX");

    EXPECT_EQ(s.aeron.media_driver_dir, "/tmp/aeron");
    EXPECT_EQ(s.aeron.order.stream_id, 3001);
    EXPECT_EQ(s.aeron.exec_report.stream_id, 3002);
    EXPECT_EQ(s.aeron.heartbeat.stream_id, 3003);
    EXPECT_EQ(s.aeron.order.channel, "aeron:ipc");

    EXPECT_EQ(s.heimdall.heartbeat_interval_ms, 2000u);
    EXPECT_EQ(s.heimdall.stale_order_timeout_ms, 60000u);

    EXPECT_FALSE(s.heimdall.risk.trading_enabled);
    EXPECT_DOUBLE_EQ(s.heimdall.risk.max_order_size_usd, 2500.0);
    EXPECT_DOUBLE_EQ(s.heimdall.risk.max_notional_per_order_usd, 10000.0);
    EXPECT_EQ(s.heimdall.risk.max_open_orders_per_venue, 20u);
    EXPECT_EQ(s.heimdall.risk.max_orders_per_second, 5u);

    ASSERT_EQ(s.heimdall.adapters.size(), 1u);
    const auto& a = s.heimdall.adapters[0];
    EXPECT_EQ(a.exchange, "OKX");
    EXPECT_TRUE(a.testnet);
    EXPECT_EQ(a.ws_host, "wseeapap.okx.com");
    EXPECT_EQ(a.ws_port, "8443");
    EXPECT_EQ(a.ws_path, "/ws/v5/private");
    EXPECT_TRUE(a.use_tls);

    EXPECT_EQ(s.metrics_port, 9103u);
}

// ── Defaults ──────────────────────────────────────────────────────────────────

TEST(HeimdallConfigTest, DefaultsAppliedWhenFieldsOmitted) {
    // Minimal config — just enough for the loader not to throw.
    auto path = write_toml(R"(
exchanges = ["OKX"]

[[adapters]]
exchange = "OKX"
)");

    auto s = heimdall::config::load(path.string());

    EXPECT_TRUE(s.environment.empty());
    EXPECT_EQ(s.aeron.order.stream_id, 3001);
    EXPECT_EQ(s.aeron.exec_report.stream_id, 3002);
    EXPECT_EQ(s.aeron.heartbeat.stream_id, 3003);
    EXPECT_EQ(s.aeron.order.channel, "aeron:ipc");

    EXPECT_EQ(s.heimdall.heartbeat_interval_ms, 1000u);
    EXPECT_EQ(s.heimdall.stale_order_timeout_ms, 30000u);
    EXPECT_TRUE(s.heimdall.risk.trading_enabled);
    EXPECT_DOUBLE_EQ(s.heimdall.risk.max_order_size_usd, 1000.0);
    EXPECT_EQ(s.metrics_port, 9103u);

    ASSERT_EQ(s.heimdall.adapters.size(), 1u);
    EXPECT_FALSE(s.heimdall.adapters[0].testnet);
    EXPECT_TRUE(s.heimdall.adapters[0].use_tls);
    EXPECT_EQ(s.heimdall.adapters[0].rest_port, "443");
    EXPECT_EQ(s.heimdall.adapters[0].ws_port, "443");
}

// ── Exchange filter ───────────────────────────────────────────────────────────

TEST(HeimdallConfigTest, ExchangeFilterExcludesUnlistedAdapters) {
    auto path = write_toml(R"(
exchanges = ["OKX"]

[[adapters]]
exchange = "OKX"
ws_host  = "wseeapap.okx.com"

[[adapters]]
exchange = "BINANCE"
ws_host  = "stream.binance.com"
)");

    auto s = heimdall::config::load(path.string());

    ASSERT_EQ(s.heimdall.adapters.size(), 1u);
    EXPECT_EQ(s.heimdall.adapters[0].exchange, "OKX");
}

TEST(HeimdallConfigTest, MultipleExchangesLoaded) {
    auto path = write_toml(R"(
exchanges = ["OKX", "BINANCE"]

[[adapters]]
exchange = "OKX"

[[adapters]]
exchange = "BINANCE"

[[adapters]]
exchange = "HYPERLIQUID"
)");

    auto s = heimdall::config::load(path.string());

    ASSERT_EQ(s.heimdall.adapters.size(), 2u);
    std::vector<std::string> loaded;
    for (const auto& a : s.heimdall.adapters) loaded.push_back(a.exchange);
    EXPECT_TRUE(std::find(loaded.begin(), loaded.end(), "OKX") != loaded.end());
    EXPECT_TRUE(std::find(loaded.begin(), loaded.end(), "BINANCE") != loaded.end());
}

TEST(HeimdallConfigTest, EmptyExchangeFilterLoadsNoAdapters) {
    // No `exchanges` key → exchange_filter is empty → count("OKX") == 0 → no adapters loaded.
    auto path = write_toml(R"(
[[adapters]]
exchange = "OKX"
)");

    auto s = heimdall::config::load(path.string());

    EXPECT_TRUE(s.heimdall.adapters.empty());
}

// ── Adapter fields ────────────────────────────────────────────────────────────

TEST(HeimdallConfigTest, AdapterRestHostAndPortParsed) {
    auto path = write_toml(R"(
exchanges = ["BINANCE"]

[[adapters]]
exchange  = "BINANCE"
rest_host = "api.binance.com"
rest_port = "443"
ws_host   = "stream.binance.com"
ws_port   = "9443"
use_tls   = true
)");

    auto s = heimdall::config::load(path.string());

    ASSERT_EQ(s.heimdall.adapters.size(), 1u);
    const auto& a = s.heimdall.adapters[0];
    EXPECT_EQ(a.rest_host, "api.binance.com");
    EXPECT_EQ(a.rest_port, "443");
    EXPECT_EQ(a.ws_host, "stream.binance.com");
    EXPECT_EQ(a.ws_port, "9443");
    EXPECT_TRUE(a.use_tls);
}

// ── Custom stream IDs ─────────────────────────────────────────────────────────

TEST(HeimdallConfigTest, CustomAeronStreamIds) {
    auto path = write_toml(R"(
exchanges = []

[aeron.order]
stream_id = 5001

[aeron.exec_report]
stream_id = 5002

[aeron.heartbeat]
stream_id = 5003
)");

    auto s = heimdall::config::load(path.string());

    EXPECT_EQ(s.aeron.order.stream_id, 5001);
    EXPECT_EQ(s.aeron.exec_report.stream_id, 5002);
    EXPECT_EQ(s.aeron.heartbeat.stream_id, 5003);
}

// ── Error handling ────────────────────────────────────────────────────────────

TEST(HeimdallConfigTest, MissingFileThrows) {
    EXPECT_THROW(heimdall::config::load("/nonexistent/path/heimdall.toml"), std::exception);
}
