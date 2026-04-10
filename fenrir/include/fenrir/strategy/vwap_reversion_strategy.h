#pragma once

#include "fenrir/config/config.h"
#include "fenrir/md/md_client.h"
#include "fenrir/order/order_manager.h"
#include "fenrir/refdata/refdata_client.h"
#include "fenrir/strategy/canonical_resolver.h"
#include "fenrir/strategy/i_strategy.h"
#include "fenrir/strategy/position_tracker.h"

#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/ExecutionReport.h>
#include <bifrost_protocol/MdMarketData.h>
#include <bifrost_protocol/MdTrade.h>
#include <bifrost_protocol/OrderSide.h>

#include <atomic>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fenrir::strategy {

// VWAP-reversion strategy.
//
// Per instrument:
//   1. Maintain a rolling VWAP from trade ticks (window = vwap_window_trades).
//   2. On each BBO tick compute:
//        deviation = (mid - vwap) / vwap
//   3. Entry (no open position, no open order, cooldown elapsed):
//        deviation > +entry_threshold  → SELL  (price stretched above VWAP; expect reversion)
//        deviation < -entry_threshold  → BUY   (price stretched below VWAP; expect reversion)
//   4. Exit (have open position, no open order):
//        Long  → SELL when mid ≥ vwap × (1 - exit_threshold)   [reverted]
//             OR SELL when mid ≤ avg_entry × (1 - stop_threshold) [stop loss]
//        Short → BUY  when mid ≤ vwap × (1 + exit_threshold)   [reverted]
//             OR BUY  when mid ≥ avg_entry × (1 + stop_threshold) [stop loss]
class VwapReversionStrategy : public IStrategy {
public:
    VwapReversionStrategy(uint64_t correlation_id,
                          const config::StrategyConfig& cfg,
                          refdata::RefdataClient& refdata,
                          md::MdClient* md,
                          order::OrderManager* order_mgr);

    void start() override;
    void on_snapshot(const refdata::InstrumentCache& cache) override;
    void on_delta(const refdata::Instrument& inst, bifrost::protocol::DeltaUpdateType::Value update_type) override;
    void on_bbo(const bifrost::protocol::MdMarketData& tick) override;
    void on_trade(const bifrost::protocol::MdTrade& tick) override;
    void on_exec_report(const bifrost::protocol::ExecutionReport& rpt) override;

private:
    struct InstrumentState {
        // Rolling VWAP window — (price, qty) pairs in normal units
        std::deque<std::pair<double, double>> trade_window;
        double vwap_pxqty_sum{0.0};
        double vwap_qty_sum{0.0};

        // One open order per instrument at a time (0 = none)
        uint64_t open_order_id{0};
        bifrost::protocol::OrderSide::Value open_order_side{bifrost::protocol::OrderSide::NULL_VALUE};

        // Cooldown — suppress new entry signals for cooldown_ns_ after the last one
        uint64_t last_signal_ns{0};

        // Instrument metadata
        std::string symbol;
        std::string exchange;
        bifrost::protocol::ExchangeId::Value exchange_id{bifrost::protocol::ExchangeId::NULL_VALUE};
    };

    void send_order(uint64_t instrument_id,
                    InstrumentState& st,
                    bifrost::protocol::OrderSide::Value side,
                    double mid,
                    double vwap,
                    double quantity,
                    uint64_t timestamp_ns,
                    const char* reason);

    uint64_t correlation_id_;

    // Config params
    std::size_t vwap_window_trades_;
    std::size_t min_trades_to_signal_;
    double entry_threshold_;
    double exit_threshold_;
    double stop_threshold_;
    uint64_t cooldown_ns_;

    std::vector<std::string> instruments_;
    std::vector<std::string> md_exchanges_;
    std::unordered_map<std::string, config::VenueExecConfig> venue_exec_;

    refdata::RefdataClient& refdata_;
    md::MdClient* md_client_;
    order::OrderManager* order_mgr_;
    std::unordered_map<uint64_t, InstrumentState> state_;  // keyed by instrument_id
    PositionTracker positions_;
    std::unordered_map<uint64_t, uint64_t> order_to_instrument_;  // order_id → instrument_id
};

}  // namespace fenrir::strategy
