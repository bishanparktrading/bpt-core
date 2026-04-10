#include "fenrir/strategy/vwap_reversion_strategy.h"

#include <bifrost_protocol/DeltaUpdateType.h>
#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/ExecStatus.h>
#include <bifrost_protocol/InstrumentType.h>
#include <bifrost_protocol/OrderType.h>
#include <bifrost_protocol/RejectSource.h>
#include <bifrost_protocol/TimeInForce.h>

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

using bifrost::protocol::ExchangeId;
using bifrost::protocol::ExecStatus;
using bifrost::protocol::OrderSide;
using bifrost::protocol::OrderType;
using bifrost::protocol::RejectSource;
using bifrost::protocol::TimeInForce;

namespace fenrir::strategy {

static constexpr double kOrderQty = 0.001;  // 0.001 base unit in natural units

VwapReversionStrategy::VwapReversionStrategy(uint64_t correlation_id,
                                             const config::StrategyConfig& cfg,
                                             refdata::RefdataClient& refdata,
                                             md::MdClient* md,
                                             order::OrderManager* order_mgr)
    : correlation_id_(correlation_id),
      vwap_window_trades_(static_cast<std::size_t>(cfg.params["vwap_window_trades"].value<int64_t>().value_or(200))),
      min_trades_to_signal_(static_cast<std::size_t>(cfg.params["min_trades_to_signal"].value<int64_t>().value_or(50))),
      entry_threshold_(cfg.params["entry_threshold"].value<double>().value_or(0.002)),
      exit_threshold_(cfg.params["exit_threshold"].value<double>().value_or(0.0005)),
      stop_threshold_(cfg.params["stop_threshold"].value<double>().value_or(0.005)),
      cooldown_ns_(static_cast<uint64_t>(cfg.params["cooldown_ms"].value<int64_t>().value_or(2000)) * 1'000'000ULL),
      instruments_(cfg.instruments),
      md_exchanges_(cfg.md_exchanges),
      venue_exec_(cfg.venue_exec),
      refdata_(refdata),
      md_client_(md),
      order_mgr_(order_mgr) {
    spdlog::info(
        "[VwapReversion] vwap_window={} min_trades={} entry={:.4f}% exit={:.4f}% "
        "stop={:.4f}% cooldown_ms={}",
        vwap_window_trades_,
        min_trades_to_signal_,
        entry_threshold_ * 100.0,
        exit_threshold_ * 100.0,
        stop_threshold_ * 100.0,
        cooldown_ns_ / 1'000'000ULL);
    spdlog::info("[VwapReversion] risk: max_position_usd={} max_order_size_usd={} max_daily_loss_usd={}",
                 cfg.risk.max_position_usd,
                 cfg.risk.max_order_size_usd,
                 cfg.risk.max_daily_loss_usd);
    for (const auto& s : instruments_)
        spdlog::info("[VwapReversion] instrument: {}", s);
}

// ── IStrategy ──────────────────────────────────────────────────────────────

void VwapReversionStrategy::start() {
    for (const auto& ex : md_exchanges_)
        spdlog::info("[VwapReversion] MD exchange: {}", ex);

    std::vector<refdata::RefdataClient::CanonicalFilter> filters;
    for (const auto& sym : instruments_) {
        if (auto parsed = CanonicalResolver::parse(sym)) {
            const auto sbe_type = [&]() {
                using T = refdata::InstrumentType;
                using S = bifrost::protocol::InstrumentType;
                switch (parsed->type) {
                    case T::SPOT:
                        return S::SPOT;
                    case T::PERPETUAL:
                        return S::PERPETUAL;
                    case T::FUTURE:
                        return S::FUTURE;
                    case T::OPTION:
                        return S::OPTION;
                    default:
                        return S::NULL_VALUE;
                }
            }();
            if (md_exchanges_.empty()) {
                filters.push_back({parsed->base, parsed->quote, sbe_type, ""});
            } else {
                for (const auto& ex : md_exchanges_)
                    filters.push_back({parsed->base, parsed->quote, sbe_type, ex});
            }
        }
    }
    refdata_.subscribe(correlation_id_, std::move(filters));
}

void VwapReversionStrategy::on_snapshot(const refdata::InstrumentCache& cache) {
    spdlog::info("[VwapReversion] Snapshot ({} instruments), resolving universe...", cache.size());
    state_.clear();
    order_to_instrument_.clear();
    positions_.clear_all();

    const auto ids = CanonicalResolver::resolve(cache, instruments_, md_exchanges_);
    for (uint64_t id : ids) {
        const auto inst = cache.get(id);
        if (!inst)
            continue;

        auto ex_id = ExchangeId::NULL_VALUE;
        if (inst->exchange == "BINANCE")
            ex_id = ExchangeId::BINANCE;
        else if (inst->exchange == "OKX")
            ex_id = ExchangeId::OKX;
        else if (inst->exchange == "HYPERLIQUID")
            ex_id = ExchangeId::HYPERLIQUID;

        state_.emplace(id, InstrumentState{.symbol = inst->symbol, .exchange = inst->exchange, .exchange_id = ex_id});
        spdlog::info("  [{}] {} @ {}", id, inst->symbol, inst->exchange);
    }

    spdlog::info("[VwapReversion] Trading universe: {} instrument(s)", state_.size());

    if (!md_client_)
        return;

    std::vector<md::MdClient::InstrumentDesc> subs;
    subs.reserve(state_.size());
    for (const auto& [id, st] : state_)
        subs.push_back({id, st.exchange, st.symbol});

    spdlog::info("[VwapReversion] Subscribing MD to {} instrument(s)", subs.size());
    md_client_->subscribe(correlation_id_, subs);
}

void VwapReversionStrategy::on_delta(const refdata::Instrument& inst,
                                     bifrost::protocol::DeltaUpdateType::Value update_type) {
    if (update_type == bifrost::protocol::DeltaUpdateType::ADD) {
        const auto ids = CanonicalResolver::resolve(refdata_.cache(), instruments_, md_exchanges_);
        if (std::find(ids.begin(), ids.end(), inst.instrument_id) == ids.end())
            return;

        using EX = bifrost::protocol::ExchangeId;
        auto ex_id = EX::NULL_VALUE;
        if (inst.exchange == "BINANCE")
            ex_id = EX::BINANCE;
        else if (inst.exchange == "OKX")
            ex_id = EX::OKX;
        else if (inst.exchange == "HYPERLIQUID")
            ex_id = EX::HYPERLIQUID;

        state_.emplace(inst.instrument_id,
                       InstrumentState{.symbol = inst.symbol, .exchange = inst.exchange, .exchange_id = ex_id});
        spdlog::info("[VwapReversion] Delta ADD {} @ {}", inst.symbol, inst.exchange);

    } else if (update_type == bifrost::protocol::DeltaUpdateType::REMOVE) {
        state_.erase(inst.instrument_id);
        spdlog::info("[VwapReversion] Delta REMOVE {} @ {}", inst.symbol, inst.exchange);
    }
}

void VwapReversionStrategy::on_trade(const bifrost::protocol::MdTrade& tick) {
    auto it = state_.find(tick.instrumentId());
    if (it == state_.end())
        return;

    InstrumentState& st = it->second;

    const double price = tick.price();
    const double qty = tick.qty();
    if (qty <= 0.0 || price <= 0.0)
        return;

    // Add to rolling window
    st.trade_window.emplace_back(price, qty);
    st.vwap_pxqty_sum += price * qty;
    st.vwap_qty_sum += qty;

    // Evict oldest entry when window is full
    if (st.trade_window.size() > vwap_window_trades_) {
        const auto [old_px, old_qty] = st.trade_window.front();
        st.trade_window.pop_front();
        st.vwap_pxqty_sum -= old_px * old_qty;
        st.vwap_qty_sum -= old_qty;
        // Guard against floating-point drift going negative
        if (st.vwap_qty_sum < 0.0)
            st.vwap_qty_sum = 0.0;
    }
}

void VwapReversionStrategy::on_bbo(const bifrost::protocol::MdMarketData& tick) {
    auto it = state_.find(tick.instrumentId());
    if (it == state_.end())
        return;

    InstrumentState& st = it->second;

    // Need enough trades before trusting the VWAP
    if (st.trade_window.size() < min_trades_to_signal_)
        return;
    if (st.vwap_qty_sum <= 0.0)
        return;

    const double vwap = st.vwap_pxqty_sum / st.vwap_qty_sum;
    const double mid = (tick.bidPrice() + tick.askPrice()) * 0.5;

    const int64_t net = positions_.net_qty(tick.instrumentId(), st.exchange_id);
    const bool has_position = net != 0;
    const bool has_order = st.open_order_id != 0;

    if (!has_position && !has_order) {
        // ── Entry ─────────────────────────────────────────────────────────
        if (tick.timestampNs() < st.last_signal_ns + cooldown_ns_)
            return;

        const double dev = (mid - vwap) / vwap;
        if (dev > +entry_threshold_) {
            send_order(tick.instrumentId(),
                       st,
                       bifrost::protocol::OrderSide::SELL,
                       mid,
                       vwap,
                       kOrderQty,
                       tick.timestampNs(),
                       "ENTRY SHORT");
        } else if (dev < -entry_threshold_) {
            send_order(tick.instrumentId(),
                       st,
                       bifrost::protocol::OrderSide::BUY,
                       mid,
                       vwap,
                       kOrderQty,
                       tick.timestampNs(),
                       "ENTRY LONG");
        }

    } else if (has_position && !has_order) {
        // ── Exit / stop ───────────────────────────────────────────────────
        const auto pos = positions_.get(tick.instrumentId(), st.exchange_id);
        if (!pos)
            return;

        const uint64_t close_qty = static_cast<uint64_t>(std::abs(net));

        if (net > 0) {
            // Long: sell to exit when price has reverted up toward VWAP, or stop out
            const bool revert = mid >= vwap * (1.0 - exit_threshold_);
            const bool stop = mid <= pos->avg_price * (1.0 - stop_threshold_);
            if (revert || stop) {
                send_order(tick.instrumentId(),
                           st,
                           bifrost::protocol::OrderSide::SELL,
                           mid,
                           vwap,
                           close_qty,
                           tick.timestampNs(),
                           stop ? "STOP LONG" : "EXIT LONG");
            }
        } else {
            // Short: buy to exit when price has reverted down toward VWAP, or stop out
            const bool revert = mid <= vwap * (1.0 + exit_threshold_);
            const bool stop = mid >= pos->avg_price * (1.0 + stop_threshold_);
            if (revert || stop) {
                send_order(tick.instrumentId(),
                           st,
                           bifrost::protocol::OrderSide::BUY,
                           mid,
                           vwap,
                           close_qty,
                           tick.timestampNs(),
                           stop ? "STOP SHORT" : "EXIT SHORT");
            }
        }
    }
}

void VwapReversionStrategy::on_exec_report(const bifrost::protocol::ExecutionReport& rpt) {
    const uint64_t order_id = rpt.orderId();
    const uint64_t instrument_id = rpt.instrumentId();
    const auto status = rpt.status();

    auto inst_it = order_to_instrument_.find(order_id);
    if (inst_it == order_to_instrument_.end())
        return;

    // Use the canonical instrument_id stored at order placement — the exec
    // report's instrumentId() may be 0 if the gateway doesn't carry canonical IDs.
    const uint64_t canonical_id = inst_it->second;
    auto state_it = state_.find(canonical_id);
    if (state_it == state_.end())
        return;

    InstrumentState& st = state_it->second;

    if (status == ExecStatus::ACKED) {
        spdlog::debug("[VwapReversion] ExecReport order_id={} {} {} ACKED", order_id, st.symbol, st.exchange);
    } else if (status == ExecStatus::REJECTED) {
        const auto src = rpt.rejectSource();
        const bool gateway_reject = (src == RejectSource::GATEWAY || src == RejectSource::RISK);
        if (gateway_reject)
            spdlog::error("[VwapReversion] ExecReport order_id={} {} {} REJECTED reason={} source={}",
                          order_id,
                          st.symbol,
                          st.exchange,
                          bifrost::protocol::RejectReason::c_str(rpt.rejectReason()),
                          bifrost::protocol::RejectSource::c_str(src));
        else
            spdlog::warn("[VwapReversion] ExecReport order_id={} {} {} REJECTED reason={} source={}",
                         order_id,
                         st.symbol,
                         st.exchange,
                         bifrost::protocol::RejectReason::c_str(rpt.rejectReason()),
                         bifrost::protocol::RejectSource::c_str(src));
    } else {
        spdlog::info("[VwapReversion] ExecReport order_id={} {} {} status={} filled={:.6f} price={:.2f}",
                     order_id,
                     st.symbol,
                     st.exchange,
                     bifrost::protocol::ExecStatus::c_str(status),
                     static_cast<double>(rpt.filledQty()) / 1e5,
                     static_cast<double>(rpt.price()) / 1e8);
    }

    if (status == ExecStatus::FILLED || status == ExecStatus::PARTIAL) {
        positions_.on_fill(canonical_id, st.exchange_id, rpt.side(), rpt.filledQty(), rpt.price());

        if (const auto pos = positions_.get(canonical_id, st.exchange_id)) {
            spdlog::info("[VwapReversion] Position {} @ {}  net_qty={}  avg_price={:.6f}  rpnl={:.4f}",
                         st.symbol,
                         st.exchange,
                         pos->net_qty,
                         pos->avg_price,
                         pos->realized_pnl);
        }
    }

    // Terminal statuses: clear the open-order slot
    if (status == ExecStatus::FILLED || status == ExecStatus::CANCELLED || status == ExecStatus::REJECTED) {
        if (st.open_order_id == order_id) {
            st.open_order_id = 0;
            st.open_order_side = bifrost::protocol::OrderSide::NULL_VALUE;
        }
        order_to_instrument_.erase(order_id);
    }
    // PARTIAL: order still live — keep open_order_id so no duplicate is sent
    // ACKED:   order acknowledged but not yet filled — keep open_order_id
}

// ── Private ────────────────────────────────────────────────────────────────

void VwapReversionStrategy::send_order(uint64_t instrument_id,
                                       InstrumentState& st,
                                       bifrost::protocol::OrderSide::Value side,
                                       double mid,
                                       double vwap,
                                       double quantity,
                                       uint64_t timestamp_ns,
                                       const char* reason) {
    const auto vex_it = venue_exec_.find(st.exchange);
    if (vex_it == venue_exec_.end() || !vex_it->second.enabled) {
        spdlog::debug("[VwapReversion] Venue {} not enabled — signal suppressed", st.exchange);
        return;
    }
    const auto& vex = vex_it->second;

    const auto order_type = (vex.order_type == "MARKET") ? OrderType::MARKET : OrderType::LIMIT;
    const auto tif = (vex.tif == "IOC") ? TimeInForce::IOC : (vex.tif == "FOK") ? TimeInForce::FOK : TimeInForce::GTC;

    // Price: aggressive limit (0.1% over/under mid) so it crosses the spread;
    // zero for MARKET orders.
    const double price_f = (order_type == OrderType::MARKET) ? 0.0
                           : (side == OrderSide::BUY)        ? mid * 1.001
                                                             : mid * 0.999;

    if (!order_mgr_) {
        spdlog::info("[VwapReversion] {} {} {} @ {} mid={:.6f} vwap={:.6f} (no gateway)",
                     reason,
                     (side == OrderSide::BUY ? "BUY" : "SELL"),
                     st.symbol,
                     st.exchange,
                     mid,
                     vwap);
        return;
    }

    spdlog::info("[VwapReversion] {} {} {} @ {} mid={:.6f} vwap={:.6f} dev={:+.4f}%",
                 reason,
                 (side == OrderSide::BUY ? "BUY" : "SELL"),
                 st.symbol,
                 st.exchange,
                 mid,
                 vwap,
                 ((mid - vwap) / vwap) * 100.0);

    const uint64_t order_id =
        order_mgr_->place_order(instrument_id, st.exchange_id, side, order_type, tif, price_f, quantity);
    if (order_id != 0) {
        spdlog::info("[VwapReversion] order placed → order_id={}", order_id);
        st.open_order_id = order_id;
        st.open_order_side = side;
        st.last_signal_ns = timestamp_ns;
        order_to_instrument_[order_id] = instrument_id;
    }
}

}  // namespace fenrir::strategy
