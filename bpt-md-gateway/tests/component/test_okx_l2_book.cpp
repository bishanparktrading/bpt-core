// Component tests for OKX L2 order book parsing — verifies that
// multi-level book snapshots are correctly parsed into MdOrderBook
// structs, which feed the OFI strategy's OFICalculator.

#include "fake_md_publisher.h"
#include "md_gateway/adapter/common/subscription_map.h"
#include "md_gateway/adapter/okx/okx_md_decoder.h"

#include <gtest/gtest.h>

namespace bpt::md_gateway::adapter {
namespace {

struct OkxL2Fixture {
    SubscriptionMap subs;
    OkxMdDecoder<test::FakeMdPublisher> parser{subs};
    test::FakeMdPublisher pub;
    messaging::FundingRateCallback fr_cb;

    void inject(const char* msg, uint64_t recv_ns = 0) { parser.decode(msg, recv_ns, pub, fr_cb); }
};

// ---------------------------------------------------------------------------
// Multi-level book
// ---------------------------------------------------------------------------

TEST(OkxL2BookTest, ThreeLevelBook) {
    OkxL2Fixture f;
    f.subs.subscribe(1001, "BTC-USDT-SWAP", 5);

    f.inject(
        R"({"arg":{"channel":"books5","instId":"BTC-USDT-SWAP"},"data":[{
            "bids":[["29990","1.5","0","1"],["29989","2.0","0","2"],["29988","3.0","0","1"]],
            "asks":[["29991","0.8","0","1"],["29992","1.2","0","3"],["29993","0.5","0","1"]]
        }]})",
        111ULL);

    // Should publish both BBO and order book
    ASSERT_TRUE(f.pub.last_bbo.has_value());
    ASSERT_TRUE(f.pub.last_order_book.has_value());

    const auto& book = *f.pub.last_order_book;
    EXPECT_EQ(book.instrument_id, 1001ULL);
    EXPECT_EQ(book.timestamp_ns, 111ULL);

    ASSERT_EQ(book.bids.size(), 3u);
    EXPECT_DOUBLE_EQ(book.bids[0].first, 29990.0);
    EXPECT_DOUBLE_EQ(book.bids[0].second, 1.5);
    EXPECT_DOUBLE_EQ(book.bids[1].first, 29989.0);
    EXPECT_DOUBLE_EQ(book.bids[1].second, 2.0);
    EXPECT_DOUBLE_EQ(book.bids[2].first, 29988.0);
    EXPECT_DOUBLE_EQ(book.bids[2].second, 3.0);

    ASSERT_EQ(book.asks.size(), 3u);
    EXPECT_DOUBLE_EQ(book.asks[0].first, 29991.0);
    EXPECT_DOUBLE_EQ(book.asks[0].second, 0.8);
    EXPECT_DOUBLE_EQ(book.asks[1].first, 29992.0);
    EXPECT_DOUBLE_EQ(book.asks[1].second, 1.2);
    EXPECT_DOUBLE_EQ(book.asks[2].first, 29993.0);
    EXPECT_DOUBLE_EQ(book.asks[2].second, 0.5);
}

// ---------------------------------------------------------------------------
// Depth capping
// ---------------------------------------------------------------------------

TEST(OkxL2BookTest, DepthCappedBySubscription) {
    OkxL2Fixture f;
    // Subscribed with depth=2, even though exchange sends 5 levels
    f.subs.subscribe(1001, "BTC-USDT-SWAP", 2);

    f.inject(
        R"({"arg":{"channel":"books5","instId":"BTC-USDT-SWAP"},"data":[{
            "bids":[["29990","1.5","0","1"],["29989","2.0","0","2"],["29988","3.0","0","1"],["29987","4.0","0","1"],["29986","5.0","0","1"]],
            "asks":[["29991","0.8","0","1"],["29992","1.2","0","3"],["29993","0.5","0","1"],["29994","2.0","0","1"],["29995","3.0","0","1"]]
        }]})",
        222ULL);

    ASSERT_TRUE(f.pub.last_order_book.has_value());
    const auto& book = *f.pub.last_order_book;
    EXPECT_EQ(book.bids.size(), 2u);
    EXPECT_EQ(book.asks.size(), 2u);
}

// ---------------------------------------------------------------------------
// No order book when depth=0 (BBO only subscription)
// ---------------------------------------------------------------------------

TEST(OkxL2BookTest, NoOrderBookWhenDepthZero) {
    OkxL2Fixture f;
    f.subs.subscribe(1001, "BTC-USDT-SWAP", 0);

    f.inject(
        R"({"arg":{"channel":"books5","instId":"BTC-USDT-SWAP"},"data":[{
            "bids":[["29990","1.5","0","1"]],
            "asks":[["29991","0.8","0","1"]]
        }]})",
        333ULL);

    // BBO should still publish
    ASSERT_TRUE(f.pub.last_bbo.has_value());
    // But no order book (depth=0 means BBO-only)
    EXPECT_FALSE(f.pub.last_order_book.has_value());
}

// ---------------------------------------------------------------------------
// Single level still publishes both BBO and book when depth >= 1
// ---------------------------------------------------------------------------

TEST(OkxL2BookTest, SingleLevelPublishesBothBboAndBook) {
    OkxL2Fixture f;
    f.subs.subscribe(1001, "BTC-USDT-SWAP", 5);

    f.inject(
        R"({"arg":{"channel":"books5","instId":"BTC-USDT-SWAP"},"data":[{
            "bids":[["29990","1.5","0","1"]],
            "asks":[["29991","0.8","0","1"]]
        }]})",
        444ULL);

    EXPECT_TRUE(f.pub.last_bbo.has_value());
    ASSERT_TRUE(f.pub.last_order_book.has_value());
    EXPECT_EQ(f.pub.last_order_book->bids.size(), 1u);
    EXPECT_EQ(f.pub.last_order_book->asks.size(), 1u);
}

// ---------------------------------------------------------------------------
// Stateful `books` channel — snapshot + update deltas
// ---------------------------------------------------------------------------
// OKX's `books` channel (depth>5) is delta-based: the first message is a
// full snapshot, subsequent messages carry only changed levels. The
// parser has to maintain a per-instrument book state so the published
// BBO reflects actual top-of-book, not the top of the delta slice.

TEST(OkxL2BookTest, SnapshotThenUpdateTracksBbo) {
    OkxL2Fixture f;
    f.subs.subscribe(1001, "BTC-USDT-SWAP", 10);

    // Snapshot: best bid 100, best ask 101.
    f.inject(
        R"({"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"snapshot","data":[{
            "bids":[["100","1","0","1"],["99","2","0","1"],["98","3","0","1"]],
            "asks":[["101","1","0","1"],["102","2","0","1"],["103","3","0","1"]]
        }]})",
        100ULL);
    ASSERT_TRUE(f.pub.last_bbo.has_value());
    EXPECT_DOUBLE_EQ(f.pub.last_bbo->bid_price, 100.0);
    EXPECT_DOUBLE_EQ(f.pub.last_bbo->ask_price, 101.0);

    // Update touches only deep levels (98 and 103). Top of book must be
    // unchanged — before the fix the old code would have published
    // bbo={bid=98, ask=103} because it read the top of the delta slice.
    f.inject(
        R"({"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"update","data":[{
            "bids":[["98","5","0","1"]],
            "asks":[["103","7","0","1"]]
        }]})",
        200ULL);
    ASSERT_TRUE(f.pub.last_bbo.has_value());
    EXPECT_DOUBLE_EQ(f.pub.last_bbo->bid_price, 100.0) << "Top bid must not follow the delta";
    EXPECT_DOUBLE_EQ(f.pub.last_bbo->ask_price, 101.0) << "Top ask must not follow the delta";
}

TEST(OkxL2BookTest, UpdateWithZeroQtyRemovesLevel) {
    OkxL2Fixture f;
    f.subs.subscribe(1001, "BTC-USDT-SWAP", 10);

    f.inject(
        R"({"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"snapshot","data":[{
            "bids":[["100","1","0","1"],["99","2","0","1"]],
            "asks":[["101","1","0","1"],["102","2","0","1"]]
        }]})",
        100ULL);

    // Remove best bid (qty==0 on 100). Top should fall to 99.
    f.inject(
        R"({"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"update","data":[{
            "bids":[["100","0","0","0"]],
            "asks":[]
        }]})",
        200ULL);
    ASSERT_TRUE(f.pub.last_bbo.has_value());
    EXPECT_DOUBLE_EQ(f.pub.last_bbo->bid_price, 99.0);
    EXPECT_DOUBLE_EQ(f.pub.last_bbo->ask_price, 101.0);
}

TEST(OkxL2BookTest, UpdateAddsNewBestLevel) {
    OkxL2Fixture f;
    f.subs.subscribe(1001, "BTC-USDT-SWAP", 10);

    f.inject(
        R"({"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"snapshot","data":[{
            "bids":[["100","1","0","1"]],
            "asks":[["101","1","0","1"]]
        }]})",
        100ULL);

    // Update inserts a new tighter level on each side.
    f.inject(
        R"({"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"update","data":[{
            "bids":[["100.5","1","0","1"]],
            "asks":[["100.7","1","0","1"]]
        }]})",
        200ULL);
    ASSERT_TRUE(f.pub.last_bbo.has_value());
    EXPECT_DOUBLE_EQ(f.pub.last_bbo->bid_price, 100.5);
    EXPECT_DOUBLE_EQ(f.pub.last_bbo->ask_price, 100.7);
}

TEST(OkxL2BookTest, MultipleInstrumentsIsolated) {
    OkxL2Fixture f;
    f.subs.subscribe(1001, "BTC-USDT-SWAP", 10);
    f.subs.subscribe(1002, "ETH-USDT-SWAP", 10);

    // Snapshot BTC, best bid 100.
    f.inject(
        R"({"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"snapshot","data":[{
            "bids":[["100","1","0","1"]],
            "asks":[["101","1","0","1"]]
        }]})",
        100ULL);
    // Snapshot ETH, best bid 3000.
    f.inject(
        R"({"arg":{"channel":"books","instId":"ETH-USDT-SWAP"},"action":"snapshot","data":[{
            "bids":[["3000","1","0","1"]],
            "asks":[["3001","1","0","1"]]
        }]})",
        200ULL);
    // Update ETH — BTC state must not leak in / out.
    f.inject(
        R"({"arg":{"channel":"books","instId":"ETH-USDT-SWAP"},"action":"update","data":[{
            "bids":[["2999","2","0","1"]],
            "asks":[]
        }]})",
        300ULL);
    // ETH now holds levels 3000 and 2999.
    ASSERT_TRUE(f.pub.last_order_book.has_value());
    EXPECT_EQ(f.pub.last_order_book->instrument_id, 1002ULL);
    EXPECT_EQ(f.pub.last_order_book->bids.size(), 2u);
    EXPECT_DOUBLE_EQ(f.pub.last_order_book->bids[0].first, 3000.0);
    EXPECT_DOUBLE_EQ(f.pub.last_order_book->bids[1].first, 2999.0);

    // Now poke BTC with an update. Its state must still be intact from
    // the earlier snapshot — the update should merge into it.
    f.inject(
        R"({"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"update","data":[{
            "bids":[["99","2","0","1"]],
            "asks":[]
        }]})",
        400ULL);
    ASSERT_TRUE(f.pub.last_order_book.has_value());
    EXPECT_EQ(f.pub.last_order_book->instrument_id, 1001ULL);
    EXPECT_EQ(f.pub.last_order_book->bids.size(), 2u);
    EXPECT_DOUBLE_EQ(f.pub.last_order_book->bids[0].first, 100.0);
    EXPECT_DOUBLE_EQ(f.pub.last_order_book->bids[1].first, 99.0);
}

TEST(OkxL2BookTest, SecondSnapshotClearsStaleLevels) {
    OkxL2Fixture f;
    f.subs.subscribe(1001, "BTC-USDT-SWAP", 10);

    // Snapshot with 3 bid levels.
    f.inject(
        R"({"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"snapshot","data":[{
            "bids":[["100","1","0","1"],["99","2","0","1"],["98","3","0","1"]],
            "asks":[["101","1","0","1"]]
        }]})",
        100ULL);

    // Fresh snapshot with only 1 bid level — old 99/98 levels must be
    // discarded (a snapshot is authoritative).
    f.inject(
        R"({"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"snapshot","data":[{
            "bids":[["100","1","0","1"]],
            "asks":[["101","1","0","1"]]
        }]})",
        200ULL);
    ASSERT_TRUE(f.pub.last_order_book.has_value());
    EXPECT_EQ(f.pub.last_order_book->bids.size(), 1u);
    EXPECT_DOUBLE_EQ(f.pub.last_order_book->bids[0].first, 100.0);
}

}  // namespace
}  // namespace bpt::md_gateway::adapter
