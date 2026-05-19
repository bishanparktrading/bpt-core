#pragma once

/// \file
/// \brief OptionsMakerStrategy — two-sided option-chain market making.
///
/// Quotes bid + ask on a filtered option universe (front N expiries, ATM strike
/// band) using POST_ONLY orders priced off the live IV surface from bpt-pricer.
/// Aggregates Greeks across all option positions per underlying; delta is
/// expected to be neutralised by a separate `bpt-hedger` service that
/// subscribes to PortfolioSnapshot — this strategy does NOT emit perp orders.
///
/// Reuses ShortVolStrategy's per-(exchange, underlying) state shape but with:
///   - Two-sided quote state per option (bid_order_id + ask_order_id), not a
///     single fill-and-forget order
///   - POST_ONLY exec_inst on every quote (cross == regression)
///   - Vega-aware half-spread instead of fixed edge in price
///   - Vega-budget sizing instead of fixed notional
///
/// This header carries the skeleton ONLY — IStrategy callbacks are stubbed.
/// The next chunk wires:
///   1. Theo computation from VolSurface
///   2. Quote price/size calc (half-spread from vega, size from vega budget)
///   3. POST_ONLY emission via OrderManager
///   4. Fill handling + re-quote loop
///   5. Pre-trade Greek limits

#include "strategy/config/config.h"
#include "strategy/md/md_client.h"
#include "strategy/order/order_manager.h"
#include "strategy/refdata/refdata_client.h"
#include "strategy/strategy/i_strategy.h"
#include "strategy/strategy/position_tracker.h"
#include "strategy/vol/vol_surface_client.h"

#include <messages/ExchangeId.h>
#include <messages/ExecutionReport.h>
#include <messages/MdMarketData.h>
#include <messages/MdTrade.h>
#include <messages/VolSurface.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bpt::strategy::strategy {

class OptionsMakerStrategy : public IStrategy {
public:
    OptionsMakerStrategy(uint64_t correlation_id,
                         const config::StrategyConfig& cfg,
                         refdata::IRefdataClient& refdata,
                         md::IMdClient* md,
                         order::OrderManager* order_mgr,
                         vol::VolSurfaceClient* vol_client);

    void start() override;
    void on_snapshot(const refdata::InstrumentCache& cache) override;
    void on_delta(const refdata::Instrument& inst, bpt::messages::DeltaUpdateType::Value update_type) override;
    void on_bbo(const bpt::messages::MdMarketData& tick) override;
    void on_trade(const bpt::messages::MdTrade& tick) override;
    void on_vol_surface(bpt::messages::VolSurface& surface) override;
    void on_exec_report(const bpt::messages::ExecutionReport& rpt) override;
    std::size_t on_account_snapshot(bpt::messages::AccountSnapshot& snap) override;
    PortfolioState get_portfolio_state() override;
    std::string get_strategy_state_json() override;
    void on_shutdown_flatten() override;
    [[nodiscard]] bool has_pending_flatten() const override;

private:
    /// Per-option quote + position state. Reflects the latest surface input
    /// and the strategy's resting bid/ask.
    struct OptionQuote {
        // Identity
        uint64_t instrument_id{0};
        uint32_t expiry_date{0};
        double strike{0.0};
        bool is_call{true};
        double time_to_expiry_y{0.0};

        // Latest theo + Greeks from VolSurface
        double iv{0.0};
        double theo_price{0.0};
        double delta{0.0};
        double gamma{0.0};
        double vega{0.0};
        double theta{0.0};

        // Venue BBO snapshot — set on every VolSurface tick from pt.bidPrice/
        // askPrice. Zero when the strike has no observed venue maker
        // (interpolated SVI point). Used as the POST_ONLY cap reference and
        // as the price reference for shutdown_flatten IOC crosses.
        double venue_bid_price{0.0};
        double venue_ask_price{0.0};

        // OUR resting quotes (0 = no live order on that side). bid_price /
        // ask_price are our limit prices, NOT the venue's — they're only
        // written when we successfully place an order, never from surface
        // decode. Cleared via cancel_quote_side when the order is gone.
        uint64_t bid_order_id{0};
        uint64_t ask_order_id{0};
        double bid_price{0.0};
        double ask_price{0.0};
        double bid_qty{0.0};
        double ask_qty{0.0};

        // Position
        double position_qty{0.0};  // signed; + = long
        double entry_price{0.0};

        // Per-option cadence timestamp — was state-wide last_quote_ns before
        // 2026-05-15, which broke after the first option's requote in a tick
        // (the rest skipped on throttle). Per-option semantics now match the
        // intent: each strike gets refreshed at most once per
        // requote_min_interval_ns_, independent of its neighbours.
        uint64_t last_quote_ns{0};
    };

    /// Per-underlying aggregate state. Greeks summed across all options the
    /// strategy is quoting on this underlying. Perp BBO captured for the
    /// hedger to consume via PortfolioSnapshot, not used to send orders here.
    struct UnderlyingState {
        std::string underlying;
        bpt::messages::ExchangeId::Value exchange_id{};

        // Option universe + live quotes
        std::unordered_map<uint64_t, OptionQuote> options;

        // Perp hedge leg — strategy emits IOC orders to neutralise the
        // delta accumulated from option fills. Hedger is embedded (not a
        // separate service) for v1 simplicity; extract when a second
        // options strategy actually needs the same hedger.
        uint64_t perp_instrument_id{0};
        double perp_bid{0.0};
        double perp_ask{0.0};
        double perp_position_qty{0.0};  ///< signed; + long perp / − short
        uint64_t perp_order_id{0};      ///< 0 = no in-flight hedge
        uint64_t last_hedge_ns{0};

        // Aggregate Greeks across all option positions
        double portfolio_delta{0.0};
        double portfolio_gamma{0.0};
        double portfolio_vega{0.0};
        double portfolio_theta{0.0};

        // Forward price + active quoting universe (filtered to front-N
        // expiries × ATM strike band). Filled on every VolSurface tick.
        double forward_price{0.0};
        std::unordered_set<uint64_t> active_instruments;

        // Phase 2 (synthetic quoting) — populated on every VolSurface tick:
        // last_surface_ns gates against stale smiles, observed_strikes_by_expiry
        // is the lookup the wing-distance safeguard uses to verify each
        // synthetic strike has an observed-data anchor nearby in the same
        // expiry. Sorted ascending so std::lower_bound finds neighbors cheaply.
        uint64_t last_surface_ns{0};
        std::unordered_map<uint32_t, std::vector<double>> observed_strikes_by_expiry;
    };

    // Helpers
    void resolve_option_universe(const refdata::InstrumentCache& cache);
    void recompute_greeks(UnderlyingState& state);
    void requote(UnderlyingState& state, OptionQuote& opt, uint64_t now_ns);
    void cancel_quote_side(OptionQuote& opt, bool bid_side);
    /// Evaluate book delta vs the hedge threshold and fire an IOC perp order
    /// if a hedge is needed and no hedge order is already in flight.
    void maybe_hedge(UnderlyingState& state, uint64_t now_ns);

    // Config — captured at construction. The body of the cpp constructor
    // reads these from `cfg.params`.
    uint32_t front_n_expiries_{1};        ///< quote only the front N expiries on each underlying
    uint32_t max_strikes_per_expiry_{8};  ///< ATM band size around forward
    double risk_free_rate_{0.0};          ///< r for BS-from-fitted-IV theo; 0.0 fine for short-dated crypto options
    bool quote_synthetic_strikes_{
        false};  ///< Phase 2 gate: when true, also quote strikes with no venue BBO (theo from SVI fit only)
    double synthetic_size_mult_{
        0.25};  ///< Phase 2 safeguard: scale quote size on synthetic strikes (sole-maker → lower confidence)
    double synthetic_max_strike_distance_pct_{
        0.05};  ///< Phase 2 safeguard: skip synthetic strike if nearest observed-same-expiry strike farther than this
                ///< fraction of forward
    uint64_t synthetic_smile_staleness_ns_{
        30'000'000'000ULL};  ///< Phase 2 safeguard: skip synthetic quoting if no surface tick within this window
    double vega_edge_vol_pts_{0.005};                   ///< half-spread in vol points (0.005 = 50 bps of IV)
    double per_quote_vega_budget_{20.0};                ///< vega per single live quote
    double max_book_vega_{500.0};                       ///< hard limit on aggregate |vega|
    double max_book_delta_{1.0};                        ///< hard limit on aggregate |delta| (BTC units)
    double max_book_gamma_{50.0};                       ///< hard limit on aggregate |gamma|
    uint64_t requote_min_interval_ns_{500'000'000ULL};  ///< 500ms min cadence per option

    // Hedger config — embedded, not a separate service for v1.
    bool enable_hedger_{true};                    ///< master kill-switch for the embedded hedger
    double max_hedge_abs_delta_{0.05};            ///< hedge when |book_delta| exceeds this (BTC units)
    double hedge_aggress_bps_{10.0};              ///< cross perp BBO by this many bps for IOC fills
    uint64_t hedge_cooldown_ns_{500'000'000ULL};  ///< 500ms anti-thrash window between hedges
    double book_delta_sanity_ceiling_mult_{
        20.0};  ///< |book_delta| > this × max_hedge_abs_delta latches risk_halted_ (restart to clear)

    /// Latched on book-delta sanity-ceiling breach (or any other future
    /// hard-halt condition). Suppresses both `maybe_hedge` and `requote`
    /// for the rest of the process lifetime — restart to clear, deliberate
    /// so a human reviews before trading resumes.
    bool risk_halted_{false};

    // Shutdown flatten config — IOC-close positions when the service stops.
    bool shutdown_flatten_positions_{true};  ///< master switch: send IOC LIMITs to close net positions on shutdown
    double shutdown_perp_cross_bps_{20.0};   ///< cross perp mid by this many bps for the IOC close

    // Standard plumbing
    uint64_t correlation_id_;
    std::vector<std::string> underlyings_;
    std::vector<std::string> md_exchanges_;
    std::unordered_map<std::string, config::VenueExecConfig> venue_exec_;
    config::RiskConfig risk_;

    refdata::IRefdataClient& refdata_;
    md::IMdClient* md_client_;
    order::OrderManager* order_mgr_;
    vol::VolSurfaceClient* vol_client_;

    // Keyed by "exchange_id:underlying" — mirrors ShortVolStrategy's
    // canonical key shape so the console can resolve cross-strategy
    // state consistently.
    std::unordered_map<std::string, UnderlyingState> states_;
    std::unordered_map<uint64_t, std::string> instrument_to_key_;
    std::unordered_map<uint64_t, std::string> order_to_key_;
    std::unordered_map<uint64_t, bool> order_is_bid_side_;
    /// Discriminates perp hedge orders from option quote orders when an exec
    /// report comes back — option fills change book Greeks, perp fills change
    /// the hedge leg.
    std::unordered_map<uint64_t, bool> order_is_perp_hedge_;

    /// Every successful place_order is recorded here keyed by order_id, and
    /// removed when a terminal exec report (FILLED / CANCELLED / REJECTED)
    /// arrives. This is the canonical "what do we have alive on the
    /// exchange" view that survives the per-option bid_order_id/ask_order_id
    /// being cleared by cancel-and-replace — without it, shutdown_flatten
    /// previously iterated cleared ids and missed real orphans (2026-05-15
    /// post-mortem: 18 live Deribit orders went uncancelled on shutdown).
    struct OrderRef {
        bpt::messages::ExchangeId::Value exchange_id;
        uint64_t instrument_id;
    };
    std::unordered_map<uint64_t, OrderRef> live_orders_;

    PositionTracker positions_;

    static std::string state_key(bpt::messages::ExchangeId::Value ex, const std::string& underlying);
};

}  // namespace bpt::strategy::strategy
