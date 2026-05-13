/// \file matching_engine_book.cpp
/// \brief Synthetic-L3 slot-queue helpers + L2 snapshot reconciliation.
///
/// Holds: per-level slot deque lookup, the L2-snapshot → slot-deque
/// reconciliation (reconcile_l2_snapshot), the cancel attribution rule
/// (distribute_cancels), and book_qty_at_price for crossing-check use.
/// Lifecycle / dispatch lives in matching_engine.cpp; fill computation
/// lives in matching_engine_fills.cpp.

#include "backtester/data/orderbook_record.h"
#include "backtester/matching/matching_engine.h"

#include <algorithm>
#include <bpt_common/logging.h>
#include <cmath>

namespace bpt::backtester::matching {

std::deque<MatchingEngine::Slot>& MatchingEngine::level_queue(const std::string& book_key,
                                                              OrderSide side,
                                                              double price) {
    auto& pq = slot_queues_[book_key];
    auto& m = (side == OrderSide::BUY) ? pq.bid : pq.ask;
    return m[price_key(price)];
}

std::deque<MatchingEngine::Slot>* MatchingEngine::level_queue_if_exists(const std::string& book_key,
                                                                        OrderSide side,
                                                                        double price) {
    auto qit = slot_queues_.find(book_key);
    if (qit == slot_queues_.end())
        return nullptr;
    auto& m = (side == OrderSide::BUY) ? qit->second.bid : qit->second.ask;
    auto pit = m.find(price_key(price));
    if (pit == m.end())
        return nullptr;
    return &pit->second;
}

void MatchingEngine::distribute_cancels(std::deque<Slot>& queue, double cancels) {
    if (cancels <= 0.0 || queue.empty())
        return;

    // Linear end-weighted attribution: cancel rate at queue position i is
    // proportional to i, so the share of cancels falling in a slot covering
    // position range [d_start, d_end] is (d_end² − d_start²) / N². This is
    // the discrete-slot generalisation of the continuous formula in Phase 2;
    // for a queue with a single venue slot covering the full range it
    // reduces to the same answer. Our own slots are skipped — we don't
    // model spontaneous cancellation of our own resting orders.
    double total_qty = 0.0;
    for (const auto& s : queue)
        total_qty += s.qty;
    if (total_qty <= 0.0)
        return;

    const double N2 = total_qty * total_qty;

    double remaining_cancels = cancels;
    double d = 0.0;
    for (auto& slot : queue) {
        if (remaining_cancels <= 0.0)
            break;
        const double d_start = d;
        const double d_end = d + slot.qty;
        d = d_end;
        if (slot.is_ours)
            continue;
        const double share_fraction = (d_end * d_end - d_start * d_start) / N2;
        double slot_cancel = cancels * share_fraction;
        if (slot_cancel > slot.qty)
            slot_cancel = slot.qty;
        if (slot_cancel > remaining_cancels)
            slot_cancel = remaining_cancels;
        slot.qty -= slot_cancel;
        remaining_cancels -= slot_cancel;
    }

    // Cap-and-redistribute: if some slots couldn't absorb their full share
    // (small back slot under heavy back-weighting), spread the residual
    // proportionally across remaining venue capacity. Single pass — for
    // realistic cancel volumes the residual is small and a second pass is
    // overkill.
    if (remaining_cancels > 1e-12) {
        double avail = 0.0;
        for (const auto& s : queue)
            if (!s.is_ours)
                avail += s.qty;
        if (avail > 0.0) {
            for (auto& slot : queue) {
                if (remaining_cancels <= 0.0)
                    break;
                if (slot.is_ours)
                    continue;
                double take = remaining_cancels * (slot.qty / avail);
                if (take > slot.qty)
                    take = slot.qty;
                slot.qty -= take;
                remaining_cancels -= take;
            }
        }
    }

    // Erase fully-cancelled venue slots. Our slots stay even at qty=0 — they
    // get removed via cancel_order or the post-fill cleanup in
    // fill_against_trade, not by L2 reconciliation.
    queue.erase(std::remove_if(queue.begin(), queue.end(), [](const Slot& s) { return !s.is_ours && s.qty <= 1e-12; }),
                queue.end());
}

void MatchingEngine::reconcile_l2_snapshot(const std::string& book_key, const data::OrderBookRecord& new_book) {
    // Track which (side, price_key) levels were touched so we can leave the
    // rest alone (a level that drops off L5 keeps its slot queue intact
    // until it reappears — we don't know what's there now, and discarding
    // would lose any resting orders we have at it).
    auto reconcile_one = [&](OrderSide side, double price, double observed_size) {
        if (observed_size <= 0.0)
            return;
        auto& q = level_queue(book_key, side, price);
        // L2 snapshots report VENUE size only — our resting orders aren't
        // visible to the venue book (we're a backtester, not a real client
        // hitting the matching engine). Compare observed against the
        // venue-portion of our slot deque.
        double expected_venue = 0.0;
        for (const auto& s : q)
            if (!s.is_ours)
                expected_venue += s.qty;
        const double delta = observed_size - expected_venue;
        if (delta > 1e-12) {
            // Venue volume joined between snapshots — append a single inferred
            // slot at the back. Real venue arrivals could be multiple distinct
            // orders, but L2 gives us only the aggregate.
            q.push_back(Slot{/*qty=*/delta, /*is_ours=*/false, /*our_order_id=*/{}, current_ts_});
        } else if (delta < -1e-12) {
            // Cancels (any trade-print consumption already happened during
            // fill_against_trade, so the slot state reflects post-trade reality).
            distribute_cancels(q, -delta);
        }
    };

    for (int lvl = 0; lvl < data::kOrderBookDepth; ++lvl) {
        if (new_book.bid_px[lvl] > 0.0 && new_book.bid_sz[lvl] > 0.0)
            reconcile_one(OrderSide::BUY, new_book.bid_px[lvl], new_book.bid_sz[lvl]);
        if (new_book.ask_px[lvl] > 0.0 && new_book.ask_sz[lvl] > 0.0)
            reconcile_one(OrderSide::SELL, new_book.ask_px[lvl], new_book.ask_sz[lvl]);
    }
}

double MatchingEngine::book_qty_at_price(const data::OrderBookRecord& book, OrderSide side, double price) {
    constexpr double kPriceTol = 1e-9;
    if (side == OrderSide::BUY) {
        for (int i = 0; i < data::kOrderBookDepth; ++i) {
            if (book.bid_px[i] <= 0.0)
                continue;
            if (std::abs(book.bid_px[i] - price) < kPriceTol)
                return book.bid_sz[i];
        }
    } else {
        for (int i = 0; i < data::kOrderBookDepth; ++i) {
            if (book.ask_px[i] <= 0.0)
                continue;
            if (std::abs(book.ask_px[i] - price) < kPriceTol)
                return book.ask_sz[i];
        }
    }
    return 0.0;
}

}  // namespace bpt::backtester::matching
