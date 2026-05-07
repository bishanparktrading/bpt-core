// Unit tests for bpt::backtester::matching::MatchingEngine
#include "backtester/data/market_event.h"
#include "backtester/data/orderbook_record.h"
#include "backtester/data/trade_record.h"
#include "backtester/matching/matching_engine.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace bpt::backtester::matching;
using namespace bpt::backtester::data;

// ── Helpers ───────────────────────────────────────────────────────────────────

static MarketEvent make_book(const std::string& exchange,
                             const std::string& symbol,
                             double bid,
                             double ask,
                             double size = 10.0,
                             uint64_t ts = 1000) {
    OrderBookRecord ob;
    ob.timestamp_ns = ts;
    ob.exchange = exchange;
    ob.symbol = symbol;
    for (int i = 0; i < kOrderBookDepth; ++i) {
        ob.bid_px[i] = bid - i * 0.01;
        ob.bid_sz[i] = size;
        ob.ask_px[i] = ask + i * 0.01;
        ob.ask_sz[i] = size;
    }
    return MarketEvent::from_orderbook(ob);
}

static OpenOrder make_order(OrderType type,
                            OrderSide side,
                            double qty,
                            double price = 0.0,
                            const std::string& oid = "ord1",
                            const std::string& coid = "client1") {
    OpenOrder o;
    o.order_id = oid;
    o.client_order_id = coid;
    o.exchange = "BINANCE";
    o.symbol = "BTCUSDT";
    o.type = type;
    o.side = side;
    o.quantity = qty;
    o.price = price;
    return o;
}

static MarketEvent make_trade(const std::string& exchange,
                              const std::string& symbol,
                              TradeSide side,
                              double price,
                              double qty,
                              uint64_t ts = 2000) {
    TradeRecord t;
    t.timestamp_ns = ts;
    t.exchange = exchange;
    t.symbol = symbol;
    t.side = side;
    t.price = price;
    t.quantity = qty;
    return MarketEvent::from_trade(t);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(MatchingEngineTest, MarketBuyFillsAtBestAsk) {
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 5.0));

    auto order = make_order(OrderType::MARKET, OrderSide::BUY, 3.0);
    auto result = eng.submit_order(order);

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_price, 101.0);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_qty, 3.0);
    EXPECT_DOUBLE_EQ(fills[0].cumulative_fill_qty, 3.0);
    EXPECT_TRUE(fills[0].is_fully_filled);
    EXPECT_DOUBLE_EQ(result.filled_qty, 3.0);
}

TEST(MatchingEngineTest, MarketSellFillsAtBestBid) {
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 5.0));

    auto result = eng.submit_order(make_order(OrderType::MARKET, OrderSide::SELL, 2.0));

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_price, 100.0);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_qty, 2.0);
    EXPECT_TRUE(fills[0].is_fully_filled);
}

TEST(MatchingEngineTest, MarketOrderWalksMultipleLevels) {
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    // Each level has size 2.0; best ask=101.0, next=101.01, ...
    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 2.0));

    eng.submit_order(make_order(OrderType::MARKET, OrderSide::BUY, 5.0));

    // 2 fills: 2@101.0 + 2@101.01 + 1@101.02 = 3 levels
    ASSERT_EQ(fills.size(), 3u);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_price, 101.0);
    EXPECT_DOUBLE_EQ(fills[1].last_fill_price, 101.01);
    EXPECT_DOUBLE_EQ(fills[2].last_fill_price, 101.02);
    EXPECT_TRUE(fills.back().is_fully_filled);
}

TEST(MatchingEngineTest, LimitBuyFillsWhenTradePrintsAtOurPrice) {
    // Queue-aware model: LIMIT fills happen when a trade prints at our
    // price (or better-for-us), not just when the BBO moves. This
    // mirrors live exchange semantics: the BBO showing your price
    // doesn't mean YOU got filled — only an explicit print does.
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    // Book: bid=101, ask=102. Our LIMIT BUY at 101 joins the existing
    // bid level (queue_ahead = 10 from the seeded snapshot).
    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 101.0, 102.0, 10.0));

    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 1.0, 101.0));
    EXPECT_EQ(fills.size(), 0u);

    // Counterparty sells 12 units at 101. Drains 10 of queue_ahead, then
    // fills our 1 unit (residual 2 unused).
    eng.on_market_event(make_trade("BINANCE", "BTCUSDT", TradeSide::SELL, 101.0, 12.0));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_price, 101.0);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_qty, 1.0);
    EXPECT_TRUE(fills[0].is_fully_filled);
}

TEST(MatchingEngineTest, LimitSellFillsWhenTradePrintsAtOurPrice) {
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    // Book: bid=98, ask=99. Our LIMIT SELL at 99 joins the existing
    // ask level (queue_ahead = 10).
    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 98.0, 99.0, 10.0));

    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::SELL, 1.0, 99.0));
    EXPECT_EQ(fills.size(), 0u);

    // Counterparty buys 12 at 99. Drains queue then fills us.
    eng.on_market_event(make_trade("BINANCE", "BTCUSDT", TradeSide::BUY, 99.0, 12.0));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_price, 99.0);
    EXPECT_TRUE(fills[0].is_fully_filled);
}

// ── Queue-aware behavior ─────────────────────────────────────────────────────

TEST(MatchingEngineTest, OrderBehindQueueDoesNotFillIfPrintTooSmall) {
    // queue_ahead = 5. A 3-unit print drains queue but doesn't reach us.
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 5.0));
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 1.0, 100.0));

    // Print of 3 < queue_ahead of 5 → no fill, queue drains to 2.
    eng.on_market_event(make_trade("BINANCE", "BTCUSDT", TradeSide::SELL, 100.0, 3.0));
    EXPECT_EQ(fills.size(), 0u);

    // Subsequent print of 3 — drains remaining 2 of queue + fills us 1.
    eng.on_market_event(make_trade("BINANCE", "BTCUSDT", TradeSide::SELL, 100.0, 3.0));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_qty, 1.0);
}

TEST(MatchingEngineTest, OrderAtNewLevelHasNoQueueAhead) {
    // Book bid=100/ask=102. A LIMIT BUY at 101 joins a NEW level
    // between bid and ask — queue_ahead = 0 (we're alone).
    // First print at 101 fills us immediately (no queue to drain).
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 102.0, 10.0));
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 1.0, 101.0));

    eng.on_market_event(make_trade("BINANCE", "BTCUSDT", TradeSide::SELL, 101.0, 1.0));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_price, 101.0);
}

TEST(MatchingEngineTest, TradeAboveLimitDoesNotFillBuy) {
    // BUY @ 100, trade prints at 101 (above our limit) → not eligible.
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 5.0));
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 1.0, 100.0));

    eng.on_market_event(make_trade("BINANCE", "BTCUSDT", TradeSide::SELL, 101.0, 10.0));
    EXPECT_EQ(fills.size(), 0u);
}

TEST(MatchingEngineTest, BuyTradeDoesNotFillBuyOrder) {
    // BUY orders only fill on SELL-side trades (taker sold into bids).
    // A BUY-side trade (taker bought from asks) shouldn't touch resting BUYs.
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 5.0));
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 1.0, 100.0));

    eng.on_market_event(make_trade("BINANCE", "BTCUSDT", TradeSide::BUY, 100.0, 100.0));
    EXPECT_EQ(fills.size(), 0u);
}

TEST(MatchingEngineTest, MakerLimitFillIsLabeledMaker) {
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 5.0));
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 1.0, 100.0));
    eng.on_market_event(make_trade("BINANCE", "BTCUSDT", TradeSide::SELL, 100.0, 10.0));

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].liquidity_role, LiquidityRole::MAKER);
}

TEST(MatchingEngineTest, MarketOrderIsLabeledTaker) {
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 5.0));
    eng.submit_order(make_order(OrderType::MARKET, OrderSide::BUY, 1.0));

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].liquidity_role, LiquidityRole::TAKER);
}

// ── Crossing-LIMIT TAKER fills ──────────────────────────────────────────────

TEST(MatchingEngineTest, CrossingLimitBuyFillsAsTakerAtTouch) {
    // Book ask=101, our LIMIT BUY @ 102 crosses → fill at 101 (the
    // ask, NOT our limit), tagged TAKER.
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 5.0));
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 1.0, 102.0));

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_price, 101.0);
    EXPECT_EQ(fills[0].liquidity_role, LiquidityRole::TAKER);
    EXPECT_EQ(fills[0].order_type, OrderType::LIMIT);
}

TEST(MatchingEngineTest, CrossingLimitSellFillsAsTakerAtTouch) {
    // Book bid=99, LIMIT SELL @ 98 crosses → fill at 99, TAKER.
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 99.0, 100.0, 5.0));
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::SELL, 1.0, 98.0));

    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_price, 99.0);
    EXPECT_EQ(fills[0].liquidity_role, LiquidityRole::TAKER);
}

TEST(MatchingEngineTest, CrossingLimitWalksMultipleLevelsAtCappedPrice) {
    // Book asks: 101 sz=2, 101.01 sz=2, 101.02 sz=2.
    // LIMIT BUY 5 @ 101.01 → walks 2@101 + 2@101.01, stops (next ask
    // 101.02 > limit). Filled 4, residual 1 rests at 101.01.
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 2.0));

    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 5.0, 101.01));

    ASSERT_EQ(fills.size(), 2u);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_price, 101.0);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_qty, 2.0);
    EXPECT_EQ(fills[0].liquidity_role, LiquidityRole::TAKER);
    EXPECT_DOUBLE_EQ(fills[1].last_fill_price, 101.01);
    EXPECT_DOUBLE_EQ(fills[1].last_fill_qty, 2.0);
    EXPECT_EQ(fills[1].liquidity_role, LiquidityRole::TAKER);
    EXPECT_FALSE(fills[1].is_fully_filled);
}

TEST(MatchingEngineTest, NonCrossingLimitDoesNotFillAtSubmit) {
    // Standard maker: BUY @ 100.5 with ask at 101 doesn't cross.
    // Should rest in pending without firing TAKER fill at submit.
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.0, 5.0));
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 1.0, 100.5));

    EXPECT_EQ(fills.size(), 0u);
}

TEST(MatchingEngineTest, CrossingLimitResidualRestsAsMaker) {
    // Cross fills 2 of 5 at touch (TAKER); residual 3 rests, then a
    // sell trade prints at our limit → maker fill on the residual.
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    // Book has only 2 units at the ask, deeper levels too high.
    OrderBookRecord ob;
    ob.timestamp_ns = 1000;
    ob.exchange = "BINANCE";
    ob.symbol = "BTCUSDT";
    ob.bid_px[0] = 99.0;  ob.bid_sz[0] = 10.0;
    ob.ask_px[0] = 100.0; ob.ask_sz[0] = 2.0;
    ob.ask_px[1] = 102.0; ob.ask_sz[1] = 10.0;  // gap; out of cross range
    eng.on_market_event(MarketEvent::from_orderbook(ob));

    // BUY 5 @ 100 crosses → fills 2 at 100 (TAKER), 3 residual rests at 100.
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 5.0, 100.0));
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_EQ(fills[0].liquidity_role, LiquidityRole::TAKER);
    EXPECT_DOUBLE_EQ(fills[0].last_fill_qty, 2.0);

    // SELL trade prints 5 at 100 → drains queue (none ahead of us at
    // our new level → queue_ahead seeded as bid_qty at price 100, which
    // is 0 since 100 wasn't an existing bid level), then fills our 3.
    eng.on_market_event(make_trade("BINANCE", "BTCUSDT", TradeSide::SELL, 100.0, 5.0));
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_EQ(fills[1].liquidity_role, LiquidityRole::MAKER);
    EXPECT_DOUBLE_EQ(fills[1].last_fill_qty, 3.0);
    EXPECT_DOUBLE_EQ(fills[1].last_fill_price, 100.0);  // limit price, not trade price
}

TEST(MatchingEngineTest, LimitOrderDoesNotFillAboveLimit) {
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 105.0));

    // Limit buy at 101, but ask is 105 — should not fill
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 1.0, 101.0));

    // Tick with ask still above limit
    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 101.0, 103.0, 10.0, 2000));
    EXPECT_EQ(fills.size(), 0u);
}

TEST(MatchingEngineTest, CancelRemovesOrder) {
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 105.0));
    eng.submit_order(make_order(OrderType::LIMIT, OrderSide::BUY, 1.0, 101.0, "ord-cancel"));

    EXPECT_TRUE(eng.cancel_order("BINANCE", "BTCUSDT", "ord-cancel"));
    EXPECT_FALSE(eng.cancel_order("BINANCE", "BTCUSDT", "ord-cancel"));  // already gone

    // Price crosses — should not fire fill
    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.5, 101.0, 10.0, 2000));
    EXPECT_EQ(fills.size(), 0u);
}

TEST(MatchingEngineTest, MarketOrderWithNoBookDoesNotCrash) {
    MatchingEngine eng;
    std::vector<FillReport> fills;
    eng.set_fill_callback([&](FillReport r) { fills.push_back(r); });

    // No book update — market order arrives
    eng.submit_order(make_order(OrderType::MARKET, OrderSide::BUY, 1.0));
    EXPECT_EQ(fills.size(), 0u);  // no book → no fill, just a warning logged
}

TEST(MatchingEngineTest, FillReportFieldsAreCorrect) {
    MatchingEngine eng;
    FillReport captured;
    eng.set_fill_callback([&](FillReport r) { captured = r; });

    eng.on_market_event(make_book("BINANCE", "BTCUSDT", 100.0, 101.5, 10.0, 5000));

    OpenOrder o = make_order(OrderType::MARKET, OrderSide::BUY, 2.0, 0.0, "myOrd", "myCid");
    eng.submit_order(o);

    EXPECT_EQ(captured.order_id, "myOrd");
    EXPECT_EQ(captured.client_order_id, "myCid");
    EXPECT_EQ(captured.symbol, "BTCUSDT");
    EXPECT_EQ(captured.exchange, "BINANCE");
    EXPECT_EQ(captured.side, OrderSide::BUY);
    EXPECT_DOUBLE_EQ(captured.original_qty, 2.0);
    EXPECT_DOUBLE_EQ(captured.last_fill_price, 101.5);
    EXPECT_EQ(captured.simulation_ts, 5000u);
}
