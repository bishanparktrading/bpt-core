/// \file matching_engine_fills.cpp
/// \brief Fill-computation member functions of MatchingEngine.
///
/// Split out of matching_engine.cpp to isolate the matching / partial-fill
/// walks from the lifecycle dispatcher. Holds: scheduling a fill on the
/// match-to-report leg, market-order walks across the book, crossing-limit
/// scans on book updates, and resting-LIMIT consumption by trade prints.

#include "backtester/data/orderbook_record.h"
#include "backtester/data/trade_record.h"
#include "backtester/matching/matching_engine.h"

#include <algorithm>
#include <bpt_common/logging.h>
#include <cmath>
#include <limits>

namespace bpt::backtester::matching {

void MatchingEngine::process_pending_submit(PendingSubmit& ps, std::vector<FillReport>& out) {
    OpenOrder& order = ps.order;

    if (order.type == OrderType::MARKET) {
        auto it = books_.find(key(order.exchange, order.symbol));
        if (it != books_.end()) {
            fill_market(order, it->second, out);
        } else {
            bpt::common::log::warn("[MatchingEngine] No book for {}/{} — market order unfilled",
                                   order.exchange,
                                   order.symbol);
        }
        return;
    }

    auto it = books_.find(key(order.exchange, order.symbol));
    if (it == books_.end()) {
        // Defensive: queue without seeding. fill_crossing_limits will pick
        // it up once a book arrives.
        pending_[key(order.exchange, order.symbol)].push_back(order);
        return;
    }
    const auto& book = it->second;

    const bool crosses =
        (order.side == OrderSide::BUY && book.ask_px[0] > 0.0 && order.price >= book.ask_px[0] - 1e-9) ||
        (order.side == OrderSide::SELL && book.bid_px[0] > 0.0 && order.price <= book.bid_px[0] + 1e-9);

    // Crossing-LIMIT: TAKER fill against the book at scheduled_match_ts.
    // Note: a POST_ONLY that wasn't synchronously rejected at submit time
    // (because the book at submit time wasn't crossing) but now crosses
    // at match time would technically be rejected here. We simplify and
    // just queue it — the case is rare in AS quoting and the alternative
    // requires a deferred-rejection exec-report path.
    if (crosses && order.type == OrderType::LIMIT) {
        fill_book_until(order, book, order.price, OrderType::LIMIT, out);
    }

    if (order.filled_qty < order.quantity - 1e-12) {
        const double remaining = order.quantity - order.filled_qty;
        const double venue_size = book_qty_at_price(book, order.side, order.price);
        const std::string bk = key(order.exchange, order.symbol);

        // Synthetic L3: append a slot for this order at the back of the
        // level deque. If we have venue volume to seed and no venue slot
        // exists yet, seed one too. Even when venue_size==0 we still add
        // our slot — orders at a brand-new price level (between bid and
        // ask) should fill on the first trade print at our price.
        auto& q = level_queue(bk, order.side, order.price);
        if (venue_size > 0.0) {
            bool has_venue_slot = false;
            for (const auto& s : q)
                if (!s.is_ours) {
                    has_venue_slot = true;
                    break;
                }
            if (!has_venue_slot)
                q.push_back(Slot{venue_size, /*is_ours=*/false, /*our_order_id=*/{}, current_ts_});
        }
        q.push_back(Slot{remaining, /*is_ours=*/true, order.order_id, current_ts_});
        order.queue_seeded = true;
        pending_[bk].push_back(order);

        bpt::common::log::debug("[MatchingEngine] Queued LIMIT {} {} {} @ {} venue_in_front={:.4f} cross_filled={:.4f}",
                                order.symbol,
                                (order.side == OrderSide::BUY ? "BUY" : "SELL"),
                                remaining,
                                order.price,
                                venue_size,
                                order.filled_qty);
    }
}

void MatchingEngine::schedule_fill(FillReport fill) {
    uint64_t latency_ns = 0;
    if (latency_)
        latency_ns = latency_->draw(fill.exchange, latency::LatencyLeg::MATCH_TO_REPORT);
    PendingFill pf;
    pf.scheduled_report_ts = fill.simulation_ts + latency_ns;
    pf.fill = std::move(fill);
    pending_fills_.push_back(std::move(pf));
}

void MatchingEngine::fill_market(OpenOrder& order, const data::OrderBookRecord& book, std::vector<FillReport>& out) {
    // MARKET = no price cap. BUY accepts any ask; SELL accepts any bid.
    const double cap = (order.side == OrderSide::BUY) ? std::numeric_limits<double>::infinity() : 0.0;
    fill_book_until(order, book, cap, OrderType::MARKET, out);

    if (order.filled_qty < order.quantity - 1e-12) {
        bpt::common::log::warn("[MatchingEngine] Market order {} partially filled: {}/{} — book too thin",
                               order.order_id,
                               order.filled_qty,
                               order.quantity);
    }
}

void MatchingEngine::fill_book_until(OpenOrder& order,
                                     const data::OrderBookRecord& book,
                                     double price_limit,
                                     OrderType report_type,
                                     std::vector<FillReport>& out) {
    double remaining = order.quantity - order.filled_qty;

    for (int lvl = 0; lvl < data::kOrderBookDepth && remaining > 1e-12; ++lvl) {
        double level_px, level_sz;
        if (order.side == OrderSide::BUY) {
            level_px = book.ask_px[lvl];
            level_sz = book.ask_sz[lvl];
        } else {
            level_px = book.bid_px[lvl];
            level_sz = book.bid_sz[lvl];
        }

        if (level_px <= 0.0 || level_sz <= 0.0)
            break;

        // Stop once the level is worse than our price cap. BUY tops out
        // when the ask rises above price_limit; SELL stops when the bid
        // drops below price_limit.
        const bool acceptable =
            (order.side == OrderSide::BUY) ? (level_px <= price_limit + 1e-9) : (level_px >= price_limit - 1e-9);
        if (!acceptable)
            break;

        double fill_qty = std::min(remaining, level_sz);
        order.filled_qty += fill_qty;
        remaining -= fill_qty;

        FillReport r;
        r.order_id = order.order_id;
        r.client_order_id = order.client_order_id;
        r.exchange = order.exchange;
        r.symbol = order.symbol;
        r.order_type = report_type;
        r.side = order.side;
        r.liquidity_role = LiquidityRole::TAKER;  // crossing the book = TAKER
        r.original_qty = order.quantity;
        // For LIMIT orders we expose the original limit price (callers
        // care about both the limit and the actual fill price); for
        // MARKET it stays 0.
        r.order_price = (report_type == OrderType::LIMIT) ? order.price : 0.0;
        r.last_fill_qty = fill_qty;
        r.last_fill_price = level_px;
        r.cumulative_fill_qty = order.filled_qty;
        r.is_fully_filled = (remaining <= 1e-12);
        r.simulation_ts = current_ts_;
        out.push_back(r);
    }
}

void MatchingEngine::fill_crossing_limits(const std::string& book_key, std::vector<FillReport>& out) {
    auto bit = books_.find(book_key);
    if (bit == books_.end())
        return;
    const auto& book = bit->second;

    if (book.bid_px[0] <= 0.0 && book.ask_px[0] <= 0.0)
        return;

    auto pit = pending_.find(book_key);
    if (pit == pending_.end())
        return;
    auto& orders = pit->second;

    orders.erase(std::remove_if(orders.begin(),
                                orders.end(),
                                [&](OpenOrder& order) {
                                    if (order.type != OrderType::LIMIT)
                                        return false;

                                    // Queue-seeded orders have a slot in the
                                    // synthetic L3 deque and fill via the
                                    // trade-print path (fill_against_trade);
                                    // skip them here to avoid double-counting.
                                    // The backstop fires only for orders that
                                    // couldn't get a slot — typically because
                                    // the book wasn't seeded yet at submit
                                    // time (no L2 snapshot received).
                                    if (order.queue_seeded)
                                        return false;

                                    double fill_px = 0.0;
                                    if (order.side == OrderSide::BUY) {
                                        // Fill if best ask has come down to or below limit price.
                                        if (book.ask_px[0] > 0.0 && book.ask_px[0] <= order.price)
                                            fill_px = book.ask_px[0];
                                    } else {
                                        // Fill if best bid has risen to or above limit price.
                                        if (book.bid_px[0] > 0.0 && book.bid_px[0] >= order.price)
                                            fill_px = book.bid_px[0];
                                    }

                                    if (fill_px <= 0.0)
                                        return false;

                                    double fill_qty = order.quantity - order.filled_qty;
                                    order.filled_qty = order.quantity;

                                    FillReport r;
                                    r.order_id = order.order_id;
                                    r.client_order_id = order.client_order_id;
                                    r.exchange = book.exchange;
                                    r.symbol = book.symbol;
                                    r.order_type = OrderType::LIMIT;
                                    r.side = order.side;
                                    // Order rested in pending_ until the
                                    // book moved to it → passive fill, MAKER.
                                    r.liquidity_role = LiquidityRole::MAKER;
                                    r.original_qty = order.quantity;
                                    r.order_price = order.price;
                                    r.last_fill_qty = fill_qty;
                                    r.last_fill_price = fill_px;
                                    r.cumulative_fill_qty = order.quantity;
                                    r.is_fully_filled = true;
                                    r.simulation_ts = current_ts_;
                                    out.push_back(r);
                                    return true;  // remove from pending
                                }),
                 orders.end());
}

void MatchingEngine::fill_against_trade(const data::TradeRecord& trade, std::vector<FillReport>& out) {
    // Counter-side semantics: a SELL trade (taker sold) consumed the bid
    // book → we walk the bid-side slot deque at the trade price. A BUY trade
    // consumed the ask book → we walk the ask-side deque.
    const OrderSide our_side = (trade.side == data::TradeSide::SELL) ? OrderSide::BUY : OrderSide::SELL;
    const std::string bk = key(trade.exchange, trade.symbol);
    auto* q = level_queue_if_exists(bk, our_side, trade.price);
    if (q == nullptr || q->empty())
        return;

    auto orders_it = pending_.find(bk);

    // Walk the slot deque from the front (FIFO), consuming as much qty as
    // fits in the remaining print. Each consumed slot either represents a
    // venue order (no FillReport emitted, just disappear) or one of our
    // orders (emit FillReport, update OpenOrder.filled_qty).
    double remaining_print = trade.quantity;
    while (remaining_print > 0.0 && !q->empty()) {
        Slot& slot = q->front();
        const double take = std::min(remaining_print, slot.qty);
        slot.qty -= take;
        remaining_print -= take;

        if (slot.is_ours && orders_it != pending_.end()) {
            // Find the OpenOrder. Pending vector is small; linear scan is fine.
            for (auto& order : orders_it->second) {
                if (order.order_id != slot.our_order_id)
                    continue;
                order.filled_qty += take;
                FillReport r;
                r.order_id = order.order_id;
                r.client_order_id = order.client_order_id;
                r.exchange = order.exchange;
                r.symbol = order.symbol;
                r.order_type = OrderType::LIMIT;
                r.side = order.side;
                r.liquidity_role = LiquidityRole::MAKER;  // resting LIMIT consumed by an incoming print.
                r.original_qty = order.quantity;
                r.order_price = order.price;
                r.last_fill_qty = take;
                r.last_fill_price = order.price;  // makers fill at their limit price, not the print's.
                r.cumulative_fill_qty = order.filled_qty;
                r.is_fully_filled = (order.filled_qty >= order.quantity - 1e-12);
                r.simulation_ts = current_ts_;
                out.push_back(r);
                break;
            }
        }

        if (slot.qty <= 1e-12)
            q->pop_front();
    }

    // Erase fully-filled queue-seeded orders from pending_. Non-seeded
    // orders (backstop path) remain — fill_crossing_limits manages them.
    if (orders_it != pending_.end()) {
        auto& orders = orders_it->second;
        orders.erase(
            std::remove_if(orders.begin(),
                           orders.end(),
                           [](const OpenOrder& o) { return o.queue_seeded && o.filled_qty >= o.quantity - 1e-12; }),
            orders.end());
    }
}

}  // namespace bpt::backtester::matching
