#pragma once

#include "fenrir/refdata/instrument_cache.h"

#include <bifrost_protocol/AccountSnapshot.h>
#include <bifrost_protocol/DeltaUpdateType.h>
#include <bifrost_protocol/ExecutionReport.h>
#include <bifrost_protocol/MdMarketData.h>
#include <bifrost_protocol/MdOrderBook.h>
#include <bifrost_protocol/MdTrade.h>
#include <bifrost_protocol/VolSurface.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fenrir::strategy {

// Snapshot of current portfolio state — queried by FenrirApp to publish
// to the dashboard bridge.  Options strategies populate this; linear
// strategies return the default (empty legs, zero Greeks).
struct PortfolioState {
    struct Leg {
        uint64_t instrument_id{0};
        std::string symbol;
        std::string underlying;
        uint32_t expiry_date{0};        // YYYYMMDD, 0 for perps
        double strike{0.0};
        bool is_call{true};
        bool is_option{true};           // false for perp hedge legs
        double qty{0.0};                // +ve = long, -ve = short
        double entry_price{0.0};
        double mark_price{0.0};         // current mid
        double iv{0.0};
        double delta{0.0};
        double gamma{0.0};
        double vega{0.0};
        double theta{0.0};
        double unrealized_pnl{0.0};
    };

    struct SurfacePoint {
        uint64_t instrument_id{0};
        uint32_t expiry_date{0};
        double strike{0.0};
        bool is_call{true};
        double iv{0.0};
        double bid_iv{0.0};
        double ask_iv{0.0};
        double delta{0.0};
        double time_to_expiry{0.0};
    };

    std::vector<Leg> legs;
    std::vector<SurfacePoint> surface_points;

    double portfolio_delta{0.0};
    double portfolio_gamma{0.0};
    double portfolio_vega{0.0};
    double portfolio_theta{0.0};
    double total_unrealized_pnl{0.0};
    double total_realized_pnl{0.0};

    uint64_t timestamp_ns{0};
};

class IStrategy {
public:
    virtual ~IStrategy() = default;

    // Activates the strategy
    virtual void start() = 0;

    // Callbacks from RefdataClient
    virtual void on_snapshot(const refdata::InstrumentCache& cache) = 0;
    virtual void on_delta(const refdata::Instrument& inst, bifrost::protocol::DeltaUpdateType::Value update_type) = 0;

    // Callbacks from MdClient
    virtual void on_bbo(const bifrost::protocol::MdMarketData& tick) = 0;
    virtual void on_trade(const bifrost::protocol::MdTrade& tick) = 0;

    // Fired when Huginn is configured with order_book_depth > 0.
    // Default no-op — only market-making strategies need to override this.
    virtual void on_order_book(const bifrost::protocol::MdOrderBook& /*book*/) {}

    // Fired when Surtr publishes a new vol surface snapshot.
    // Non-const ref: SBE group iterators are stateful and require mutation.
    // Default no-op — only options strategies need to override this.
    virtual void on_vol_surface(bifrost::protocol::VolSurface& /*surface*/) {}

    // Fired for every execution report from Heimdall.
    // Default no-op — strategies that manage positions must override this.
    virtual void on_exec_report(const bifrost::protocol::ExecutionReport& /*rpt*/) {}

    // Fired once per exchange at startup when the account snapshot is received.
    // Non-const ref: SBE group iterators are stateful.
    // Default no-op — strategies that need startup position seeding should override this.
    virtual void on_account_snapshot(bifrost::protocol::AccountSnapshot& /*snap*/) {}

    // Returns the current portfolio state for dashboard publishing.
    // Default returns empty — only options strategies with position
    // tracking need to override this.
    virtual PortfolioState get_portfolio_state() { return {}; }
};

}  // namespace fenrir::strategy
