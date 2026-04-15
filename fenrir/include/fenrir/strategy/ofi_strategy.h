#pragma once

#include "fenrir/config/config.h"
#include "fenrir/md/md_client.h"
#include "fenrir/order/order_manager.h"
#include "fenrir/refdata/refdata_client.h"
#include "fenrir/strategy/canonical_resolver.h"
#include "fenrir/strategy/i_strategy.h"
#include "fenrir/strategy/ofi_calculator.h"

#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/ExecutionReport.h>
#include <bifrost_protocol/MdMarketData.h>
#include <bifrost_protocol/MdOrderBook.h>
#include <bifrost_protocol/MdTrade.h>
#include <bifrost_protocol/OrderSide.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fenrir::strategy {

// Standalone Order-Flow-Imbalance strategy.
//
// Consumes L2 order-book updates via on_order_book() and feeds them to
// an OFICalculator per instrument. When the normalized rolling OFI
// value crosses `entry_threshold`, fires an IOC market order in the
// signal direction. Positions are exited on the first of:
//   - opposite signal crosses -exit_threshold
//   - stop/target bps from entry
//   - max_hold_seconds elapsed
// After an exit, a per-instrument cooldown (in ticks) gates re-entry
// to prevent flipping on noise.
//
// Taker-only, single position per instrument, no pyramiding. Fixed USD
// notional sizing. Requires `order_book_depth >= 1` in the strategy
// params so huginn delivers MdOrderBook frames.
class OFIStrategy : public IStrategy {
public:
    OFIStrategy(uint64_t correlation_id,
                const config::StrategyConfig& cfg,
                refdata::RefdataClient& refdata,
                md::MdClient* md,
                order::OrderManager* order_mgr);

    void start() override;
    void on_snapshot(const refdata::InstrumentCache& cache) override;
    void on_delta(const refdata::Instrument& inst, bifrost::protocol::DeltaUpdateType::Value update_type) override;
    void on_bbo(const bifrost::protocol::MdMarketData& tick) override;
    void on_trade(const bifrost::protocol::MdTrade& tick) override;
    void on_order_book(const bifrost::protocol::MdOrderBook& book) override;
    void on_exec_report(const bifrost::protocol::ExecutionReport& rpt) override;

private:
    enum class Position : uint8_t { FLAT, LONG, SHORT };

    struct InstrumentState {
        uint64_t instrument_id{0};
        std::string symbol;
        std::string exchange;
        bifrost::protocol::ExchangeId::Value exchange_id{bifrost::protocol::ExchangeId::NULL_VALUE};
        double tick_size{0.0};
        double lot_size{0.0};

        // BBO
        double bid{0.0};
        double ask{0.0};
        uint64_t last_bbo_ns{0};

        OFICalculator ofi;

        // Position
        Position pos{Position::FLAT};
        double entry_price{0.0};
        uint64_t entry_ns{0};
        uint64_t active_order_id{0};  // 0 when no order in flight

        // Cooldown after exit (in book ticks)
        int cooldown_ticks_remaining{0};

        explicit InstrumentState(OFICalculator::Config ofi_cfg) : ofi(ofi_cfg) {}
    };

    void try_enter(InstrumentState& st, double ofi_value, uint64_t now_ns);
    void try_exit(InstrumentState& st, double ofi_value, uint64_t now_ns);
    void fire_order(InstrumentState& st, bifrost::protocol::OrderSide::Value side, double qty_usd);

    uint64_t correlation_id_;

    // OFI signal config
    int book_levels_;
    uint64_t ofi_window_ns_;
    double entry_threshold_;
    double exit_threshold_;

    // Exit config
    double stop_bps_;
    double target_bps_;
    uint64_t max_hold_ns_;
    int cooldown_ticks_;

    // Execution config
    double qty_usd_;
    double max_spread_bps_;
    uint8_t order_book_depth_;

    // Standard fields
    std::vector<std::string> instruments_;
    std::vector<std::string> md_exchanges_;
    std::unordered_map<std::string, config::VenueExecConfig> venue_exec_;

    refdata::RefdataClient& refdata_;
    md::MdClient* md_client_;
    order::OrderManager* order_mgr_;
    std::unordered_map<uint64_t, InstrumentState> state_;
    std::unordered_map<uint64_t, uint64_t> order_to_instrument_;
};

}  // namespace fenrir::strategy
