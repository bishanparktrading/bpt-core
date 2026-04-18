#include "strategy/strategy/reconciler.h"

#include "strategy/strategy/position_tracker.h"

#include <gtest/gtest.h>
#include <messages/AccountSnapshot.h>
#include <messages/MessageHeader.h>

#include <array>
#include <cstring>

namespace {

using bpt::strategy::strategy::PositionTracker;
using bpt::strategy::strategy::reconcile;
using bpt::messages::AccountSnapshot;
using bpt::messages::ExchangeId;
using bpt::messages::MessageHeader;
using bpt::messages::OrderSide;

// Encode a fresh AccountSnapshot with the given positions into buf, then
// rewrap it for decode so we can hand it to reconcile(). positions is
// {exchange_symbol, net_qty_e8}. Returns a decoded-mode AccountSnapshot
// sharing the buffer.
struct Encoded {
    std::array<char, 4096> buf{};
    AccountSnapshot msg;

    explicit Encoded(ExchangeId::Value exchange,
                     const std::vector<std::pair<std::string, int64_t>>& positions) {
        AccountSnapshot writer;
        writer.wrapAndApplyHeader(buf.data(), 0, buf.size())
            .exchangeId(exchange)
            .correlationId(0)
            .timestampNs(0)
            .availableBalanceE8(0)
            .totalEquityE8(0);
        auto& group = writer.positionsCount(static_cast<uint16_t>(positions.size()));
        for (const auto& [sym, qty] : positions) {
            group.next()
                .putExchangeSymbol(sym)
                .netQtyE8(qty)
                .avgEntryPriceE8(0)
                .unrealizedPnlE8(0);
        }
        // Rewrap for decode at the same offset.
        msg.wrapForDecode(buf.data(),
                          MessageHeader::encodedLength(),
                          AccountSnapshot::sbeBlockLength(),
                          AccountSnapshot::sbeSchemaVersion(),
                          buf.size());
    }
};

constexpr int64_t kThresh = 10000;  // 0.0001 in 1e8

TEST(ReconcilerTest, EmptyTrackerEmptyExchangeNoDivergence) {
    PositionTracker t;
    Encoded snap(ExchangeId::OKX, {});
    const std::unordered_map<uint64_t, std::string> map{};
    const auto out = reconcile(t, snap.msg, map, kThresh);
    EXPECT_TRUE(out.empty());
}

TEST(ReconcilerTest, MatchingPositionNoDivergence) {
    PositionTracker t;
    // Our tracker: long 1.0 BTC on OKX for instrument 100 (symbol "BTC-USDT").
    t.on_fill(100, ExchangeId::OKX, OrderSide::BUY, 1 * 100'000'000ULL, 50000 * 100'000'000LL);
    Encoded snap(ExchangeId::OKX, {{"BTC-USDT", 1 * 100'000'000}});
    std::unordered_map<uint64_t, std::string> map{{100, "BTC-USDT"}};
    const auto out = reconcile(t, snap.msg, map, kThresh);
    EXPECT_TRUE(out.empty());
}

TEST(ReconcilerTest, DivergenceExceedsThreshold) {
    PositionTracker t;
    // We think we're long 1.0 BTC; exchange says we have 0.9 BTC.
    t.on_fill(100, ExchangeId::OKX, OrderSide::BUY, 1 * 100'000'000ULL, 50000 * 100'000'000LL);
    Encoded snap(ExchangeId::OKX, {{"BTC-USDT", 90'000'000}});  // 0.9 BTC
    std::unordered_map<uint64_t, std::string> map{{100, "BTC-USDT"}};
    const auto out = reconcile(t, snap.msg, map, kThresh);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].instrument_id, 100u);
    EXPECT_EQ(out[0].exchange_symbol, "BTC-USDT");
    EXPECT_EQ(out[0].our_net_qty_e8, 100'000'000);
    EXPECT_EQ(out[0].exchange_net_qty_e8, 90'000'000);
    EXPECT_EQ(out[0].diff_e8, 10'000'000);
}

TEST(ReconcilerTest, SmallDivergenceUnderThresholdIgnored) {
    PositionTracker t;
    t.on_fill(100, ExchangeId::OKX, OrderSide::BUY, 1 * 100'000'000ULL, 50000 * 100'000'000LL);
    // Exchange reports qty 5000 e8 less (0.00005 BTC off — below 0.0001 threshold)
    Encoded snap(ExchangeId::OKX, {{"BTC-USDT", 99'995'000}});
    std::unordered_map<uint64_t, std::string> map{{100, "BTC-USDT"}};
    const auto out = reconcile(t, snap.msg, map, kThresh);
    EXPECT_TRUE(out.empty());
}

TEST(ReconcilerTest, ExchangeMissingPositionReportsDivergence) {
    PositionTracker t;
    // We think we're long; exchange doesn't report it at all.
    t.on_fill(100, ExchangeId::OKX, OrderSide::BUY, 1 * 100'000'000ULL, 50000 * 100'000'000LL);
    Encoded snap(ExchangeId::OKX, {});  // exchange has no positions
    std::unordered_map<uint64_t, std::string> map{{100, "BTC-USDT"}};
    const auto out = reconcile(t, snap.msg, map, kThresh);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].our_net_qty_e8, 100'000'000);
    EXPECT_EQ(out[0].exchange_net_qty_e8, 0);
    EXPECT_TRUE(out[0].exchange_symbol.empty())
        << "symbol should be empty when exchange didn't report a matching position";
}

TEST(ReconcilerTest, ExchangeExtraPositionIgnored) {
    // Exchange reports ETH holdings but we don't track ETH in this
    // strategy — should be silently ignored, not flagged as a divergence.
    PositionTracker t;
    Encoded snap(ExchangeId::OKX, {{"ETH-USDT", 2 * 100'000'000}});
    std::unordered_map<uint64_t, std::string> map{};
    const auto out = reconcile(t, snap.msg, map, kThresh);
    EXPECT_TRUE(out.empty());
}

TEST(ReconcilerTest, ShortVsLongReportsDivergence) {
    PositionTracker t;
    // We think we're short 1 BTC; exchange says long 1 BTC. Sign flip =
    // a serious bug somewhere.
    t.on_fill(100, ExchangeId::OKX, OrderSide::SELL, 1 * 100'000'000ULL, 50000 * 100'000'000LL);
    Encoded snap(ExchangeId::OKX, {{"BTC-USDT", 100'000'000}});
    std::unordered_map<uint64_t, std::string> map{{100, "BTC-USDT"}};
    const auto out = reconcile(t, snap.msg, map, kThresh);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].our_net_qty_e8, -100'000'000);
    EXPECT_EQ(out[0].exchange_net_qty_e8, 100'000'000);
    EXPECT_EQ(out[0].diff_e8, -200'000'000);
}

}  // namespace
