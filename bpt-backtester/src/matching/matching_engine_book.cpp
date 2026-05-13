/// \file matching_engine_book.cpp
/// \brief Book-handling member functions of MatchingEngine.
///
/// Split out of matching_engine.cpp to keep the lifecycle / dispatch
/// concerns separate from book-level queue accounting. Same class, just
/// different translation units — C++ permits splitting member-function
/// definitions across .cpp files as long as the class declaration in the
/// header stays single-sourced.

#include "backtester/matching/matching_engine.h"

#include "backtester/data/orderbook_record.h"

#include <algorithm>
#include <cmath>

namespace bpt::backtester::matching {

void MatchingEngine::apply_queue_regen(const std::string& book_key, const data::OrderBookRecord& new_book) {
    // First book event for this instrument — nothing to compare against.
    auto old_it = books_.find(book_key);
    if (old_it == books_.end()) {
        traded_since_book_[book_key].clear();
        return;
    }
    const data::OrderBookRecord& old_book = old_it->second;

    auto pending_it = pending_.find(book_key);
    if (pending_it == pending_.end()) {
        traded_since_book_[book_key].clear();
        return;
    }

    const auto trades_at_it = traded_since_book_.find(book_key);
    const std::unordered_map<int64_t, double>* trades_at =
        (trades_at_it != traded_since_book_.end()) ? &trades_at_it->second : nullptr;

    for (auto& order : pending_it->second) {
        // Only queue-seeded orders carry meaningful queue_ahead. Backstop
        // orders (deeper than L5, or pre-book) fill via fill_crossing_limits.
        if (!order.queue_seeded)
            continue;

        const double old_size = book_qty_at_price(old_book, order.side, order.price);
        const double new_size = book_qty_at_price(new_book, order.side, order.price);
        // Level not visible in either snapshot — can't reason about cancels.
        // (Conservative: leaving queue_ahead alone here under-credits regen
        // when our level briefly drops off L5; a later book update with the
        // level back in view will resume normal accounting.)
        if (old_size <= 0.0 || new_size <= 0.0)
            continue;

        const double decrease = old_size - new_size;
        if (decrease <= 0.0)
            continue;  // level grew or unchanged; no cancels to attribute

        double traded_at_price = 0.0;
        if (trades_at) {
            const auto t_it = trades_at->find(price_key(order.price));
            if (t_it != trades_at->end())
                traded_at_price = t_it->second;
        }
        const double cancels_at_price = std::max(0.0, decrease - traded_at_price);
        if (cancels_at_price <= 0.0)
            continue;

        // Uniform-cancel attribution: under the assumption each unit on the
        // queue is equally likely to cancel, the expected number of cancels
        // ahead of us is queue_ahead × (cancels / level_size). Real micro-
        // structure is more nuanced — late-arriving size cancels less often,
        // and large icebergs reload — but uniform is the standard first cut
        // and the unbiased prior in the absence of per-order data.
        const double cancel_share = order.queue_ahead * (cancels_at_price / old_size);
        order.queue_ahead = std::max(0.0, order.queue_ahead - cancel_share);
    }

    traded_since_book_[book_key].clear();
}

double MatchingEngine::book_qty_at_price(const data::OrderBookRecord& book, OrderSide side, double price) {
    constexpr double kPriceTol = 1e-9;
    if (side == OrderSide::BUY) {
        // BUY → joins the bid queue at our price. Look for matching bid level.
        for (int i = 0; i < data::kOrderBookDepth; ++i) {
            if (book.bid_px[i] <= 0.0)
                continue;
            if (std::abs(book.bid_px[i] - price) < kPriceTol)
                return book.bid_sz[i];
        }
    } else {
        // SELL → joins the ask queue at our price.
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
