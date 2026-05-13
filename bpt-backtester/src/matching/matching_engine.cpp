/// \file matching_engine.cpp
/// \brief Lifecycle / dispatcher member functions of MatchingEngine.
///
/// This file holds the public entry points (submit_order, cancel_order,
/// on_market_event) plus the latency-aware scheduler that drains pending
/// submits and fills. Book-handling (queue regen, level lookup) lives in
/// matching_engine_book.cpp; fill computation (market walk, crossing
/// limits, trade-print consumption) lives in matching_engine_fills.cpp.
/// The class is one — only the .cpp is split.

#include "backtester/matching/matching_engine.h"

#include "backtester/data/orderbook_record.h"
#include "backtester/data/trade_record.h"

#include <algorithm>
#include <bpt_common/logging.h>
#include <cmath>

namespace bpt::backtester::matching {

// ── Internal helpers ──────────────────────────────────────────────────────────

std::string MatchingEngine::key(const std::string& exchange, const std::string& symbol) {
    return exchange + ':' + symbol;
}

int64_t MatchingEngine::price_key(double price) {
    // Scaled to nanos-of-quote-unit. 1e9 covers crypto tick sizes (HL APE
    // = 1e-5, OKX BTC swap = 1e-1, Binance options = 1e-1) with margin.
    return static_cast<int64_t>(std::llround(price * 1.0e9));
}

void MatchingEngine::set_fill_callback(FillCallback cb) {
    std::lock_guard lock(mutex_);
    fill_cb_ = std::move(cb);
}

void MatchingEngine::set_latency_model(latency::LatencyModel* model) {
    std::lock_guard lock(mutex_);
    latency_ = model;
}

// ── Market event ──────────────────────────────────────────────────────────────

void MatchingEngine::on_market_event(const data::MarketEvent& event) {
    std::vector<FillReport> deliveries;

    {
        std::lock_guard lock(mutex_);
        const uint64_t event_ts = (event.type == data::MarketEvent::Type::ORDER_BOOK)
                                      ? std::get<data::OrderBookRecord>(event.payload).timestamp_ns
                                      : std::get<data::TradeRecord>(event.payload).timestamp_ns;

        // Drain any pending submits whose scheduled match time has arrived.
        // This must run before the book update so the order matches against
        // the book as it stood between this event and the previous one — the
        // exchange's latency-affected view, not the post-event view.
        drain_pending_submits(event_ts);

        std::vector<FillReport> fills;
        if (event.type == data::MarketEvent::Type::ORDER_BOOK) {
            const auto& ob = std::get<data::OrderBookRecord>(event.payload);
            current_ts_ = ob.timestamp_ns;
            const std::string book_key = key(ob.exchange, ob.symbol);
            // Phase 5: regen queue_ahead on resting orders before the book
            // is overwritten. This lets us see the *previous* level sizes
            // alongside the new ones to attribute the delta to cancels.
            apply_queue_regen(book_key, ob);
            books_[book_key] = ob;
            fill_crossing_limits(book_key, fills);
        } else {
            const auto& t = std::get<data::TradeRecord>(event.payload);
            current_ts_ = t.timestamp_ns;
            fill_against_trade(t, fills);
        }
        for (auto& f : fills)
            schedule_fill(std::move(f));

        drain_pending_fills(current_ts_, deliveries);
    }

    for (auto& fill : deliveries)
        if (fill_cb_)
            fill_cb_(fill);
}

// ── Order submission ──────────────────────────────────────────────────────────

OpenOrder MatchingEngine::submit_order(OpenOrder order) {
    std::vector<FillReport> deliveries;

    {
        std::lock_guard lock(mutex_);
        order.submitted_ts = current_ts_;

        // Synchronous POST_ONLY-cross rejection: real exchanges return this
        // in the ack frame (HL Alo, OKX post_only, Binance LIMIT_MAKER), so
        // the order server's HTTP response can carry the error string. The
        // check uses the current book — slightly optimistic vs. checking at
        // scheduled_match_ts, but POST_ONLY-cross is rare in normal AS
        // quoting and the synchronous-ack contract is more important.
        if (order.type == OrderType::POST_ONLY) {
            auto it = books_.find(key(order.exchange, order.symbol));
            if (it != books_.end()) {
                const auto& book = it->second;
                const bool crosses =
                    (order.side == OrderSide::BUY && book.ask_px[0] > 0.0 && order.price >= book.ask_px[0] - 1e-9) ||
                    (order.side == OrderSide::SELL && book.bid_px[0] > 0.0 && order.price <= book.bid_px[0] + 1e-9);
                if (crosses) {
                    order.rejected = true;
                    bpt::common::log::debug(
                        "[MatchingEngine] POST_ONLY rejected (would cross): {} {} {} @ {} touch=({:.6f}/{:.6f})",
                        order.symbol,
                        (order.side == OrderSide::BUY ? "BUY" : "SELL"),
                        order.quantity,
                        order.price,
                        book.bid_px[0],
                        book.ask_px[0]);
                    return order;
                }
            }
        }

        // Defer the actual match by submit_to_match latency. If no model is
        // installed, scheduled_match_ts == current_ts_ and the order will drain
        // on the very next on_market_event, preserving pre-Phase-3 timing.
        uint64_t latency_ns = 0;
        if (latency_)
            latency_ns = latency_->draw(order.exchange, latency::LatencyLeg::SUBMIT_TO_MATCH);

        PendingSubmit ps;
        ps.order = order;
        ps.scheduled_match_ts = current_ts_ + latency_ns;
        pending_submits_.push_back(std::move(ps));

        // Drain any submits / fills whose scheduled times are already ≤ current_ts_.
        // With a null latency model (or zero latency for this venue) this fires the
        // just-queued order's match synchronously, preserving pre-Phase-3 semantics
        // where submit_order's fill_cb fired before return. With non-zero latency
        // this is a no-op — the order waits for a market event to advance time.
        drain_pending_submits(current_ts_);
        drain_pending_fills(current_ts_, deliveries);
    }  // unlock

    for (auto& fill : deliveries)
        if (fill_cb_)
            fill_cb_(fill);

    return order;
}

bool MatchingEngine::cancel_order(const std::string& exchange, const std::string& symbol, const std::string& order_id) {
    std::lock_guard lock(mutex_);

    // Resting orders.
    if (auto it = pending_.find(key(exchange, symbol)); it != pending_.end()) {
        auto& v = it->second;
        auto pos = std::find_if(v.begin(), v.end(), [&](const OpenOrder& o) { return o.order_id == order_id; });
        if (pos != v.end()) {
            // Decrement our_qty_ahead of trailing same-(price, side) orders
            // by the cancelled order's remaining qty. Without this, orders
            // submitted after the cancelled one would still think it sits
            // in front of them in queue.
            const double freed = pos->quantity - pos->filled_qty;
            const double cancelled_price = pos->price;
            const OrderSide cancelled_side = pos->side;
            if (freed > 0.0) {
                for (auto jit = std::next(pos); jit != v.end(); ++jit) {
                    if (jit->side == cancelled_side && std::abs(jit->price - cancelled_price) < 1e-9)
                        jit->our_qty_ahead = std::max(0.0, jit->our_qty_ahead - freed);
                }
            }
            v.erase(pos);
            return true;
        }
    }

    // Orders still in the submit-to-match latency window.
    auto it = std::find_if(pending_submits_.begin(), pending_submits_.end(), [&](const PendingSubmit& ps) {
        return ps.order.exchange == exchange && ps.order.symbol == symbol && ps.order.order_id == order_id;
    });
    if (it != pending_submits_.end()) {
        pending_submits_.erase(it);
        return true;
    }

    return false;
}

// ── Pending-submit / pending-fill machinery ──────────────────────────────────

void MatchingEngine::drain_pending_submits(uint64_t upto_ts) {
    if (pending_submits_.empty())
        return;

    // Sort by scheduled_match_ts; stable to preserve submission order
    // among orders with identical scheduled times.
    std::stable_sort(
        pending_submits_.begin(),
        pending_submits_.end(),
        [](const PendingSubmit& a, const PendingSubmit& b) { return a.scheduled_match_ts < b.scheduled_match_ts; });

    std::vector<FillReport> fills;
    auto it = pending_submits_.begin();
    for (; it != pending_submits_.end(); ++it) {
        if (it->scheduled_match_ts > upto_ts)
            break;
        // Advance current_ts_ to the order's effective arrival time so
        // queue_ahead seeding and emitted fill timestamps reflect when
        // the match actually happened.
        if (it->scheduled_match_ts > current_ts_)
            current_ts_ = it->scheduled_match_ts;
        process_pending_submit(*it, fills);
    }
    pending_submits_.erase(pending_submits_.begin(), it);

    for (auto& f : fills)
        schedule_fill(std::move(f));
}

void MatchingEngine::drain_pending_fills(uint64_t upto_ts, std::vector<FillReport>& out) {
    if (pending_fills_.empty())
        return;
    std::stable_sort(pending_fills_.begin(), pending_fills_.end(), [](const PendingFill& a, const PendingFill& b) {
        return a.scheduled_report_ts < b.scheduled_report_ts;
    });
    auto it = pending_fills_.begin();
    for (; it != pending_fills_.end(); ++it) {
        if (it->scheduled_report_ts > upto_ts)
            break;
        out.push_back(std::move(it->fill));
    }
    pending_fills_.erase(pending_fills_.begin(), it);
}

}  // namespace bpt::backtester::matching
