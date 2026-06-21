// AS graceful-exit path + account-snapshot reconcile.

#include "strategy/clock/sim_clock.h"
#include "strategy/strategy/avellaneda_stoikov_strategy.h"
#include "strategy/strategy/reconciler.h"

#include <messages/ExchangeId.h>

#include <bpt_common/logging.h>
#include <cmath>

namespace bpt::strategy::strategy {

namespace {
quill::Logger* kLog() {
    static quill::Logger* l = bpt::common::logging::get_logger("AS");
    return l;
}
}  // namespace

void AvellanedaStoikovStrategy::on_shutdown_flatten() {
    if (!order_mgr_)
        return;

    std::vector<unwind::GracefulUnwinder::Instrument> instruments;
    for (auto& [id, st] : state_) {
        order_mgr_->cancel_all(st.exchange_id, id);
        if (st.h_bid.live())
            order_mgr_->send_cancel(st.h_bid);
        if (st.h_ask.live())
            order_mgr_->send_cancel(st.h_ask);

        const double fv = st.fv.last_estimate();
        instruments.push_back({
            .instrument_id = id,
            .exchange_id = st.exchange_id,
            .tick_size = st.tick_size,
            .lot_size = st.lot_size,
            .symbol = st.symbol,
            .price_ref = (!std::isnan(fv) && fv > 0.0) ? fv : st.last_mid,
        });
    }
    unwinder_.arm(std::move(instruments));
}

void AvellanedaStoikovStrategy::on_flatten_tick() {
    unwinder_.tick();
}
bool AvellanedaStoikovStrategy::has_pending_flatten() const {
    return unwinder_.pending();
}
double AvellanedaStoikovStrategy::shutdown_drain_budget_s() const {
    return unwinder_.drain_budget_s();
}

std::size_t AvellanedaStoikovStrategy::on_account_snapshot(bpt::messages::AccountSnapshot& snap) {
    // sbeRewind() required — strategy_service calls positions().count() before handing off, advancing cursor.
    snap.sbeRewind();
    last_equity_e8_ = snap.totalEquityE8();

    const auto exchange_id = snap.exchangeId();
    const auto exchange_row_by_symbol = extract_exchange_position_rows(snap);
    std::unordered_map<std::string, int64_t> exchange_by_symbol_raw;
    exchange_by_symbol_raw.reserve(exchange_row_by_symbol.size());
    for (const auto& [symbol, row] : exchange_row_by_symbol) {
        exchange_by_symbol_raw[symbol] = row.net_qty_e8;
    }
    const auto currency_equity_e8 = extract_exchange_currency_balances(snap);
    for (const auto& [symbol, qty_e8] : exchange_by_symbol_raw) {
        last_snapshot_qty_e8_[{exchange_id, symbol}] = qty_e8;
    }
    last_snapshot_ns_ = snap.timestampNs();

    // Session-start currency baseline for SPOT reconciliation (delta from baseline = net traded).
    if (!initial_ccy_equity_captured_) {
        for (const auto& [ccy, equity] : currency_equity_e8) {
            initial_ccy_equity_e8_[{exchange_id, ccy}] = equity;
        }
        initial_ccy_equity_captured_ = true;
        bpt::common::log::info(kLog(),
                               "SPOT reconcile baseline captured: {} ccy row(s) on exchange={}",
                               currency_equity_e8.size(),
                               bpt::messages::ExchangeId::c_str(exchange_id));
    }

    std::unordered_map<std::string, int64_t> exchange_by_symbol = exchange_by_symbol_raw;
    std::unordered_map<uint64_t, std::string> symbol_map;
    symbol_map.reserve(state_.size());
    for (const auto& [id, st] : state_) {
        if (st.exchange_id != exchange_id)
            continue;
        symbol_map[id] = st.symbol;

        if (st.instrument_type == refdata::InstrumentType::SPOT && !st.base_ccy.empty()) {
            const auto it_cur = currency_equity_e8.find(st.base_ccy);
            const auto it_base = initial_ccy_equity_e8_.find({exchange_id, st.base_ccy});
            if (it_cur != currency_equity_e8.end() && it_base != initial_ccy_equity_e8_.end()) {
                exchange_by_symbol[st.symbol] = it_cur->second - it_base->second;
            } else {
                // No baseline or missing ccy row — drop symbol; reconciler compares to 0.
                exchange_by_symbol.erase(st.symbol);
            }
        }
    }
    if (symbol_map.empty())
        return 0;  // nothing we care about on this exchange

    constexpr int64_t kDivergenceThresholdE8 = 10000;  // 0.0001 base units — below min order_qty, above FP noise

    const auto divergences = reconcile(positions_, exchange_by_symbol, exchange_id, symbol_map, kDivergenceThresholdE8);
    for (const auto& d : divergences) {
        bpt::common::log::warn(kLog(),
                               "RECONCILIATION DIVERGENCE instrument_id={} symbol='{}' "
                               "our_net_qty={:.8f} exchange_net_qty={:.8f} diff={:.8f}",
                               d.instrument_id,
                               d.exchange_symbol,
                               static_cast<double>(d.our_net_qty_e8) / 1e8,
                               static_cast<double>(d.exchange_net_qty_e8) / 1e8,
                               static_cast<double>(d.diff_e8) / 1e8);

        // Seed tracker to exchange view — stale inventory from prior sessions caused wrong AS skew.
        // SPOT symbols derived from ccy-balance delta have no entry price; pass 0.0.
        double seed_avg_px = 0.0;
        if (const auto it = exchange_row_by_symbol.find(d.exchange_symbol); it != exchange_row_by_symbol.end()) {
            seed_avg_px = it->second.avg_entry_price;
        }
        positions_.seed(d.instrument_id, exchange_id, d.exchange_net_qty_e8, seed_avg_px);
        bpt::common::log::info(kLog(),
                               "reconciler: seeded position instrument_id={} symbol='{}' "
                               "to exchange view net_qty={:.8f} avg_price={:.4f}",
                               d.instrument_id,
                               d.exchange_symbol,
                               static_cast<double>(d.exchange_net_qty_e8) / 1e8,
                               seed_avg_px);
    }
    return divergences.size();
}

}  // namespace bpt::strategy::strategy
