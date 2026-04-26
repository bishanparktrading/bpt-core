#pragma once

#include "backtester/data/market_event.h"
#include "backtester/matching/open_order.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bpt::backtester::results {

// Collects fills and market prices throughout a backtest run and writes
// three output files when write() is called:
//
//   {output_dir}/trades.csv      — one row per fill
//   {output_dir}/pnl_curve.csv  — equity sampled at every fill
//   {output_dir}/summary.json   — aggregate metrics
//
// Position accounting uses the average-cost method.  Fills that close an
// existing position realise PnL immediately; fills that open or extend a
// position update the running average cost.
class ResultsCollector {
public:
    // Identity fields the archive list can use to differentiate runs without
    // opening the detail view. simulation_start/end are ISO strings copied
    // straight from the simulation config. wallclock_start_ns is captured
    // here at construction; wallclock_duration_ms is computed at write() time.
    struct RunMetadata {
        std::string simulation_start;          // ISO 8601, e.g. "2026-04-25T00:00:00Z"
        std::string simulation_end;
        std::vector<std::string> instruments;  // e.g. ["HYPERLIQUID:APE"]
        // Run identity. All optional — when empty the run still records,
        // it just can't be diff'd against a peer or replayed exactly.
        std::string strategy_name;             // e.g. "AvellanedaStoikov"
        std::string params_hash;               // sha256 of strategy config (8 chars typical)
        std::string git_sha;                   // `git rev-parse HEAD` (7 chars typical)
    };

    // Compose a deterministic run_id from the metadata + window. Used for
    // the on-disk output directory and as the primary key in archive
    // tooling. Falls back to "{start}_{end}" if the identity fields are
    // empty so older runs and ad-hoc invocations still produce a path.
    static std::string compose_run_id(const RunMetadata& m,
                                      const std::string& start_tag,
                                      const std::string& end_tag);

    ResultsCollector(double starting_capital, std::string output_dir,
                     RunMetadata metadata = {});

    // Called on every fill from MatchingEngine.
    void on_fill(const matching::FillReport& fill);

    // Called on every market event from ClockMaster.
    // Used to keep a mark-to-market mid price per symbol for unrealized PnL.
    void on_market_event(const data::MarketEvent& event);

    // Write all output files.  Creates output_dir if it does not exist.
    void write() const;

    // Exposed for testing.
    double current_equity() const;
    double compute_max_drawdown() const;
    double compute_sharpe() const;

private:
    // ── Position tracking ─────────────────────────────────────────────────

    struct Position {
        double net_qty{0};   // positive = long, negative = short
        double avg_cost{0};  // average entry price per unit
        double realized_pnl{0};
    };

    // Apply a fill to the position, return realized PnL from any closed portion.
    double apply_fill(Position& pos, double fill_qty, double fill_price, matching::OrderSide side);

    double total_realized_pnl() const;
    double total_unrealized_pnl() const;

    // ── Stored records ────────────────────────────────────────────────────

    struct TradeRow {
        uint64_t simulation_ts;
        std::string exchange;
        std::string symbol;
        std::string order_id;
        std::string client_order_id;
        std::string side;
        std::string order_type;
        double qty;
        double price;
        double realized_pnl;  // from this fill only
        double equity;        // total equity after this fill
    };

    struct EquityPoint {
        uint64_t simulation_ts;
        double equity;
    };

    // ── Data ─────────────────────────────────────────────────────────────

    double starting_capital_;
    std::string output_dir_;
    RunMetadata metadata_;
    uint64_t wallclock_start_ns_{0};

    std::unordered_map<std::string, Position> positions_;  // key = "EXCHANGE:SYMBOL"
    std::unordered_map<std::string, double> mid_prices_;   // key = "EXCHANGE:SYMBOL"

    std::vector<TradeRow> trades_;
    std::vector<EquityPoint> equity_curve_;
};

}  // namespace bpt::backtester::results
