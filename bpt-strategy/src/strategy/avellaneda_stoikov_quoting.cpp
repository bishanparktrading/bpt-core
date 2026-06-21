// AS pricing + sizing + suppression math. No order I/O.

#include "strategy/strategy/avellaneda_stoikov_strategy.h"
#include "strategy/venue/min_order_value.h"

#include <algorithm>
#include <bpt_common/logging.h>
#include <cmath>

namespace bpt::strategy::strategy {

namespace {
quill::Logger* kLog() {
    static quill::Logger* l = bpt::common::logging::get_logger("AS");
    return l;
}
}  // namespace

auto AvellanedaStoikovStrategy::compute_quotes(const InstrumentState& st, const BboContext& ctx) const
    -> std::optional<QuoteTarget> {
    if (st.ewma_var.count() < vol_warmup_ticks_)
        return std::nullopt;
    if (st.ewma_var.value() <= 0.0)
        return std::nullopt;

    const double net_qty = ctx.net_qty;
    const double mid = ctx.mid;
    const uint64_t timestamp_ns = ctx.ts_ns;

    const double elapsed_s = static_cast<double>(timestamp_ns - st.session_start_ns) * 1e-9;
    const double T_minus_t = std::max(0.0, session_duration_s_ - elapsed_s);

    const double sigma_sq = st.ewma_var.value();
    const double effective_gamma = gamma_ * st.regime.gamma_multiplier() * gamma_pnl_mult(st);
    const double gamma_sigma_sq_T = effective_gamma * sigma_sq * T_minus_t;

    // Cartea-Jaimungal drift-adjusted reservation. σ² and µ are log-returns; multiply by mid
    // for price units. q normalized to [-1,1] via max_inventory so γ is scale-invariant
    // (b684b17: unnormalized on cheap instrument blew APE bid to -$2.74 vs $0.16 mid).
    const double q_normalized =
        (sizer_.max_inventory > 0.0) ? std::clamp(net_qty / sizer_.max_inventory, -1.0, 1.0) : 0.0;
    const double inventory_skew_frac = q_normalized * gamma_sigma_sq_T;
    // ewma_drift is per-√s (log_ret/√dt); cumulative over T gives µ·√T, not µ·T.
    // µ·T overflows on slow-vol instruments — HL APE T=3600s gave drift_skew_frac=-2.52 pre-fix.
    // Suppressed during warmup — early EWMA values too noisy to project.
    double drift_skew_frac =
        (st.ewma_drift.count() >= drift_warmup_ticks_) ? st.ewma_drift.value() * std::sqrt(T_minus_t) : 0.0;
    // Cap magnitude — √(T-t) amplification at session start pushes quotes off-book without it.
    if (max_drift_skew_bps_ > 0.0) {
        const double cap = max_drift_skew_bps_ / 10000.0;
        drift_skew_frac = std::clamp(drift_skew_frac, -cap, cap);
    }
    const double ofi_skew_frac = ofi_weight_bps_ * 1e-4 * st.ofi.value();
    double book_imbalance_skew_frac = 0.0;
    if (imbalance_weight_bps_ != 0.0) {
        const double bq = st.book.best_bid_qty();
        const double aq = st.book.best_ask_qty();
        const double denom = bq + aq;
        if (denom > 0.0)
            book_imbalance_skew_frac = imbalance_weight_bps_ * 1e-4 * (bq - aq) / denom;
    }
    const double reservation =
        mid * (1.0 + drift_skew_frac + ofi_skew_frac + book_imbalance_skew_frac - inventory_skew_frac);
    const double drift_adjustment = drift_skew_frac * mid;

    // fee_half = one maker leg; ensures each half-spread covers commissions.
    double fee_half_spread = 0.0;
    const auto fee_entry = refdata_.fee_cache().get(st.exchange_id, st.instrument_id, timestamp_ns);
    if (fee_entry) {
        fee_half_spread = (static_cast<double>(fee_entry->maker_bps) / 10000.0) * mid;
    }

    const double kappa =
        (st.ewma_kappa.count() >= kappa_warmup_ticks_) ? std::max(kappa_min_, st.ewma_kappa.value()) : kappa_;

    const double min_half_spread = std::max((min_half_spread_bps_ / 10000.0) * mid, fee_half_spread);
    const double raw_half_spread =
        std::max(min_half_spread,
                 gamma_sigma_sq_T / 2.0 + (1.0 / effective_gamma) * std::log(1.0 + effective_gamma / kappa));

    // Warmup/spike safety clamp; fires when σ² blows up or κ → 0. Rate-limited WARN.
    const double max_half_spread = (max_half_spread_bps_ / 10000.0) * mid;
    double half_spread = raw_half_spread;
    if (raw_half_spread > max_half_spread) {
        half_spread = max_half_spread;
        static std::size_t clamp_count = 0;
        if (++clamp_count <= 5 || clamp_count % 1000 == 0) {
            bpt::common::log::warn(kLog(),
                                   "half-spread clamp: formula={:.2f} bps → clamped to {:.2f} bps "
                                   "(σ²={:.2e} κ={:.4f} ticks={} {}; {} clamps so far)",
                                   raw_half_spread / mid * 10000,
                                   max_half_spread_bps_,
                                   sigma_sq,
                                   kappa,
                                   st.ewma_var.count(),
                                   (st.ewma_var.count() < vol_warmup_ticks_ * 3) ? "WARMUP" : "σ-SPIKE",
                                   clamp_count);
        }
    }

    double bid = reservation - half_spread;
    double ask = reservation + half_spread;

    // Clamp to inside BBO — inventory skew can push reservation through the touch (POST_ONLY
    // reject or taker fill). Return nullopt on crossed market (transient feed state).
    if (st.tick_size > 0.0 && st.last_market_bid > 0.0 && st.last_market_ask > 0.0) {
        const double bid_cap = st.last_market_ask - st.tick_size;
        const double ask_floor = st.last_market_bid + st.tick_size;
        if (bid > bid_cap)
            bid = bid_cap;
        if (ask < ask_floor)
            ask = ask_floor;
        if (bid >= ask)
            return std::nullopt;
    }

    // b684b17: cheap-instrument formula overflow — gate here is cheaper than OM rejecting 900 orders.
    if (st.last_mid > 0.0 && quote_sanity_bps_ > 0.0) {
        const double bound = st.last_mid * (quote_sanity_bps_ / 10000.0);
        const double lo = st.last_mid - bound;
        const double hi = st.last_mid + bound;
        if (bid < lo || ask > hi || bid <= 0.0) {
            static std::size_t skip_count = 0;
            if (++skip_count <= 5 || skip_count % 1000 == 0) {
                bpt::common::log::warn(kLog(),
                                       "{} quote out of sanity range — skipping tick: "
                                       "bid={:.6f} ask={:.6f} mid={:.6f} reservation={:.6f} "
                                       "half_spread={:.6f} (sanity_bps={:.1f}; {} skips so far)",
                                       st.symbol,
                                       bid,
                                       ask,
                                       st.last_mid,
                                       reservation,
                                       half_spread,
                                       quote_sanity_bps_,
                                       skip_count);
            }
            return std::nullopt;
        }
    }

    bpt::common::log::debug(
        kLog(),
        "quotes σ²={:.2e} µ={:.2e} κ={:.4f} ({}) half_spread={:.4f} reservation={:.2f} drift_adj={:.4f}",
        sigma_sq,
        st.ewma_drift.value(),
        kappa,
        (st.ewma_kappa.count() >= kappa_warmup_ticks_) ? "live" : "fallback",
        half_spread,
        reservation,
        drift_adjustment);

    return QuoteTarget{bid, ask};
}

double AvellanedaStoikovStrategy::gamma_pnl_mult(const InstrumentState& st) const {
    // Disabled if window not configured. Also a no-op until at least
    // one fill has accrued — empty deque sums to 0, which falls into
    // the deadband by design (no over-eager widen on session start).
    if (gamma_pnl_window_n_ == 0 || st.recent_rpnl.empty())
        return 1.0;
    double sum = 0.0;
    for (double r : st.recent_rpnl)
        sum += r;
    if (sum < gamma_pnl_loss_threshold_usd_)
        return gamma_pnl_widen_mult_;
    if (sum > gamma_pnl_profit_threshold_usd_)
        return gamma_pnl_tighten_mult_;
    return 1.0;
}

// Suppression policy (per-side cancel/requote decision) moved to
// suppression_policy.h — owned by supp_policy_, called from maybe_requote
// and get_strategy_state_json via supp_policy_.evaluate(...).

}  // namespace bpt::strategy::strategy
