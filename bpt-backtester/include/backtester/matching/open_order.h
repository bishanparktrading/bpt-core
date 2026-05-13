#pragma once

/// \file
/// \brief Open-order and fill-report types used by the backtest matching engine.

#include <cstdint>
#include <string>

namespace bpt::backtester::matching {

/// \brief Order type enum.
///
/// MARKET    — always crosses the book on submission (TAKER).
/// LIMIT     — rests in pending_ if non-crossing; fills as TAKER if it
///             crosses at submit (the crossing-LIMIT path).
/// POST_ONLY — must rest as MAKER. If it would cross at submit time,
///             the matching engine rejects it (mirrors HL Alo / OKX
///             post_only / Binance LIMIT_MAKER). Never appears as a
///             TAKER fill on results, by construction.
enum class OrderType { MARKET, LIMIT, POST_ONLY };
enum class OrderSide { BUY, SELL };

/// \brief Identifies whether a fill consumed liquidity (TAKER) or provided it (MAKER).
///
/// Drives fee selection in ResultsCollector — venues typically charge taker
/// fees and either charge or rebate maker fees, so the two must be tracked
/// separately to predict P&L accurately.
///
/// Mapping in MatchingEngine:
///   MARKET orders → TAKER (always cross the book on submission)
///   LIMIT orders that rest in `pending_` and fill on a later book update
///     → MAKER (the book moved to them; they were passive)
///
/// \note The matching engine does not currently distinguish a LIMIT
/// submitted at an already-crossing price (which a real exchange would
/// fill as TAKER) — that's a separate fidelity gap from the fee model.
/// All LIMIT fills today route through fill_crossing_limits and are
/// treated as MAKER.
enum class LiquidityRole { MAKER, TAKER };

struct OpenOrder {
    std::string order_id;
    std::string client_order_id;
    std::string exchange;
    std::string symbol;
    OrderType type{OrderType::LIMIT};
    OrderSide side{OrderSide::BUY};
    double price{0.0};  ///< LIMIT price; unused for MARKET.
    double quantity{0.0};
    double filled_qty{0.0};
    uint64_t submitted_ts{0};

    // ── Queue position tracking (LIMIT orders only) ─────────────────────
    //
    // Per-order queue counters (queue_ahead, our_qty_ahead) were superseded
    // by a per-(book, side, price) slot deque in MatchingEngine. Queue
    // position is now implicit in slot order: when this order rests, a
    // Slot is appended to the level's deque and the order's qty in front
    // = sum(slot.qty for slot in deque before us). See MatchingEngine for
    // the slot mechanics.
    //
    // queue_seeded stays as the backstop indicator: true means a slot was
    // successfully added to the level deque (the price was visible in the
    // L5 snapshot at submit time), false means the order falls back to
    // fill_crossing_limits — the over-permissive legacy path retained for
    // orders deeper than L5 or submitted before any L2 snapshot arrived.
    bool queue_seeded{false};

    /// True iff the matching engine refused the order at submit time
    /// (currently only POST_ONLY orders that would cross). Returned in
    /// the OpenOrder MatchingEngine::submit_order(...) returns; the
    /// caller (each venue's order server) inspects it to send a
    /// venue-format error response back to the OGW.
    bool rejected{false};
};

struct FillReport {
    std::string order_id;
    std::string client_order_id;
    std::string exchange;
    std::string symbol;
    OrderType order_type{OrderType::LIMIT};
    OrderSide side{OrderSide::BUY};
    LiquidityRole liquidity_role{LiquidityRole::MAKER};
    double original_qty{0.0};
    double order_price{0.0};  ///< limit price of the original order.
    double last_fill_qty{0.0};
    double last_fill_price{0.0};
    double cumulative_fill_qty{0.0};
    bool is_fully_filled{false};
    uint64_t simulation_ts{0};
};

}  // namespace bpt::backtester::matching
