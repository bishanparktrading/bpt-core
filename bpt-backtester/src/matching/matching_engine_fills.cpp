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
        order.queue_ahead = book_qty_at_price(book, order.side, order.price);
        // Sum remaining qty of our already-resting orders at the same
        // (price, side). They sit in front of this new order in FIFO; the
        // total "stuff in front of me" is venue + our earlier orders.
        order.our_qty_ahead = 0.0;
        if (auto pit = pending_.find(key(order.exchange, order.symbol)); pit != pending_.end()) {
            for (const auto& o : pit->second) {
                if (o.side != order.side)
                    continue;
                if (std::abs(o.price - order.price) > 1e-9)
                    continue;
                if (!o.queue_seeded)
                    continue;  // backstop-pathway orders don't share the FIFO model
                order.our_qty_ahead += (o.quantity - o.filled_qty);
            }
        }
        order.queue_seeded = true;
        pending_[key(order.exchange, order.symbol)].push_back(order);
        bpt::common::log::debug(
            "[MatchingEngine] Queued LIMIT {} {} {} @ {} queue_ahead={:.4f} our_qty_ahead={:.4f} cross_filled={:.4f}",
            order.symbol,
            (order.side == OrderSide::BUY ? "BUY" : "SELL"),
            order.quantity - order.filled_qty,
            order.price,
            order.queue_ahead,
            order.our_qty_ahead,
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

                                    // Queue-seeded orders fill via the
                                    // trade-print path (fill_against_trade);
                                    // skip them here to avoid double-counting.
                                    // Exception: queue_ahead == 0 means the
                                    // book lookup at submit time didn't find
                                    // a level matching our price (depth-1 BBO
                                    // book, or order deeper than L5). Those
                                    // orders have no useful queue info and
                                    // should fall through to the book-cross
                                    // backstop — otherwise they're never
                                    // fillable, which is wrong for orders
                                    // that the book legitimately walks
                                    // through.
                                    if (order.queue_seeded && order.queue_ahead > 0.0)
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
    // Phase 5: accumulate trade volume by level. apply_queue_regen reads
    // this on each book update to subtract trade-attributable size before
    // attributing the rest of the level decrease to cancels. Tracked
    // unconditionally — even when we currently hold no orders, a later
    // order at this level benefits from the partial accounting.
    traded_since_book_[key(trade.exchange, trade.symbol)][price_key(trade.price)] += trade.quantity;

    auto pit = pending_.find(key(trade.exchange, trade.symbol));
    if (pit == pending_.end())
        return;
    auto& orders = pit->second;

    // Counter-side semantics: a SELL trade (taker sold) consumed the bid
    // book → only resting BUYs at price ≥ trade price participate. A BUY
    // trade consumed the ask book → only resting SELLs at price ≤ trade
    // price participate.
    //
    // Within the eligible orders, prints fan out FIFO across our own
    // resting orders ordered by submitted_ts. We take a single trade and
    // walk through orders in order — each consumes from its own queue
    // first, then fills from the residual. The next order in line sees
    // whatever volume is left after the previous order's queue+fill
    // consumption.
    //
    // Note: this doesn't model trade-volume that hits orders ahead of
    // the FIRST order in our pending list (those orders aren't ours).
    // queue_ahead absorbs that — it's the volume between the touch and
    // our first order. Correct as long as we trust the L5 snapshot.
    // Walk orders in submission order (FIFO). Each iteration may consume
    // some of the remaining trade volume. Index-iterate so we can propagate
    // venue-drain and own-fill updates forward to trailing same-(price, side)
    // orders without iterator invalidation.
    //
    // Propagation makes the model FIFO-correct when we have multiple orders
    // at the same price: a print that drains 10 units of venue volume drains
    // it for *every* later order at the same price (the venue queue is
    // shared); a fill of our earlier order shrinks the our_qty_ahead of
    // every later order at the same price (it's *our own* earlier qty in
    // front of them).
    auto same_level = [](const OpenOrder& a, const OpenOrder& b) {
        return a.side == b.side && std::abs(a.price - b.price) < 1e-9;
    };
    double remaining_print = trade.quantity;
    for (std::size_t i = 0; i < orders.size(); ++i) {
        if (remaining_print <= 0.0)
            break;
        OpenOrder& order = orders[i];
        if (order.type != OrderType::LIMIT || !order.queue_seeded)
            continue;

        const bool eligible =
            (order.side == OrderSide::BUY && trade.side == data::TradeSide::SELL &&
             order.price >= trade.price - 1e-9) ||
            (order.side == OrderSide::SELL && trade.side == data::TradeSide::BUY && order.price <= trade.price + 1e-9);
        if (!eligible)
            continue;

        // (1) Drain venue queue first. The drained amount applies to every
        // trailing same-level order — it's a shared pool, not per-order.
        const double consumed_venue = std::min(remaining_print, order.queue_ahead);
        order.queue_ahead -= consumed_venue;
        remaining_print -= consumed_venue;
        if (consumed_venue > 0.0) {
            for (std::size_t j = i + 1; j < orders.size(); ++j) {
                if (same_level(orders[j], order))
                    orders[j].queue_ahead = std::max(0.0, orders[j].queue_ahead - consumed_venue);
            }
        }
        if (remaining_print <= 0.0)
            continue;

        // (2) Drain our earlier orders ahead at this level (our_qty_ahead).
        // No propagation needed: this counter tracks earlier orders'
        // remaining qty, which already reflected previous iterations.
        const double consumed_our = std::min(remaining_print, order.our_qty_ahead);
        order.our_qty_ahead -= consumed_our;
        remaining_print -= consumed_our;
        if (remaining_print <= 0.0)
            continue;

        // (3) Fill this order from the residual print.
        const double our_remaining = order.quantity - order.filled_qty;
        const double fill_qty = std::min(remaining_print, our_remaining);
        if (fill_qty <= 0.0)
            continue;

        order.filled_qty += fill_qty;
        remaining_print -= fill_qty;
        // Propagate: trailing same-level orders saw our qty in front shrink.
        for (std::size_t j = i + 1; j < orders.size(); ++j) {
            if (same_level(orders[j], order))
                orders[j].our_qty_ahead = std::max(0.0, orders[j].our_qty_ahead - fill_qty);
        }

        FillReport r;
        r.order_id = order.order_id;
        r.client_order_id = order.client_order_id;
        r.exchange = order.exchange;
        r.symbol = order.symbol;
        r.order_type = OrderType::LIMIT;
        r.side = order.side;
        // Resting LIMIT consumed by an incoming print = MAKER, by definition.
        r.liquidity_role = LiquidityRole::MAKER;
        r.original_qty = order.quantity;
        r.order_price = order.price;
        r.last_fill_qty = fill_qty;
        // Real exchanges fill resting limits at the limit price (better
        // than the trade print for the maker), not at the print price.
        r.last_fill_price = order.price;
        r.cumulative_fill_qty = order.filled_qty;
        r.is_fully_filled = (order.filled_qty >= order.quantity - 1e-12);
        r.simulation_ts = current_ts_;
        out.push_back(r);
    }

    // Erase fully-filled queue-seeded orders. Non-seeded orders are
    // managed by fill_crossing_limits.
    orders.erase(
        std::remove_if(orders.begin(),
                       orders.end(),
                       [](const OpenOrder& o) { return o.queue_seeded && o.filled_qty >= o.quantity - 1e-12; }),
        orders.end());
}

}  // namespace bpt::backtester::matching
