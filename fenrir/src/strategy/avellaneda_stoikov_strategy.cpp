#include "fenrir/strategy/avellaneda_stoikov_strategy.h"

#include <bifrost_protocol/DeltaUpdateType.h>
#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/ExecStatus.h>
#include <bifrost_protocol/InstrumentType.h>
#include <bifrost_protocol/OrderType.h>
#include <bifrost_protocol/RejectSource.h>
#include <bifrost_protocol/TimeInForce.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <spdlog/spdlog.h>

using bifrost::protocol::ExchangeId;
using bifrost::protocol::ExecStatus;
using bifrost::protocol::OrderSide;
using bifrost::protocol::OrderType;
using bifrost::protocol::RejectSource;
using bifrost::protocol::TimeInForce;

namespace fenrir::strategy {

static constexpr double kPriceScale = 1e8;

AvellanedaStoikovStrategy::AvellanedaStoikovStrategy(uint64_t correlation_id,
                                                     const config::StrategyConfig& cfg,
                                                     refdata::RefdataClient& refdata,
                                                     md::MdClient* md,
                                                     order::OrderManager* order_mgr)
    : correlation_id_(correlation_id),
      gamma_(cfg.params["gamma"].value<double>().value_or(0.1)),
      kappa_(cfg.params["kappa"].value<double>().value_or(1.5)),
      session_duration_s_(cfg.params["session_duration_s"].value<double>().value_or(86400.0)),
      vol_halflife_s_(cfg.params["vol_halflife_s"].value<double>().value_or(60.0)),
      vol_warmup_ticks_(static_cast<std::size_t>(cfg.params["vol_warmup_ticks"].value<int64_t>().value_or(20))),
      kappa_halflife_s_(cfg.params["kappa_halflife_s"].value<double>().value_or(300.0)),
      kappa_warmup_ticks_(static_cast<std::size_t>(cfg.params["kappa_warmup_ticks"].value<int64_t>().value_or(10))),
      kappa_min_(cfg.params["kappa_min"].value<double>().value_or(0.01)),
      requote_threshold_(cfg.params["requote_threshold"].value<double>().value_or(0.0001)),
      max_inventory_(cfg.params["max_inventory"].value<double>().value_or(0.1)),
      order_qty_(cfg.params["order_qty"].value<double>().value_or(0.001)),
      min_half_spread_bps_(cfg.params["min_half_spread_bps"].value<double>().value_or(1.0)),
      instruments_(cfg.instruments),
      md_exchanges_(cfg.md_exchanges),
      venue_exec_(cfg.venue_exec),
      refdata_(refdata),
      md_client_(md),
      order_mgr_(order_mgr) {
    spdlog::info(
        "[AS] γ={:.4f} κ_fallback={:.4f} session={:.0f}s "
        "vol_halflife={:.1f}s vol_warmup={} "
        "kappa_halflife={:.1f}s kappa_warmup={} kappa_min={:.4f} "
        "requote_thr={:.4f}% max_inv={:.4f} qty={:.6f} min_spread={:.1f}bps",
        gamma_,
        kappa_,
        session_duration_s_,
        vol_halflife_s_,
        vol_warmup_ticks_,
        kappa_halflife_s_,
        kappa_warmup_ticks_,
        kappa_min_,
        requote_threshold_ * 100.0,
        max_inventory_,
        order_qty_,
        min_half_spread_bps_);
    spdlog::info("[AS] risk: max_position_usd={} max_order_size_usd={} max_daily_loss_usd={}",
                 cfg.risk.max_position_usd,
                 cfg.risk.max_order_size_usd,
                 cfg.risk.max_daily_loss_usd);

    for (const auto& s : instruments_)
        spdlog::info("[AS] instrument: {}", s);
}

// ── IStrategy ───────────────────────────────────────────────────────────────

void AvellanedaStoikovStrategy::start() {
    for (const auto& ex : md_exchanges_)
        spdlog::info("[AS] MD exchange: {}", ex);

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

void AvellanedaStoikovStrategy::on_snapshot(const refdata::InstrumentCache& cache) {
    spdlog::info("[AS] Snapshot ({} instruments), resolving universe...", cache.size());
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

        state_.emplace(id,
                       InstrumentState{.symbol = inst->symbol,
                                       .exchange = inst->exchange,
                                       .exchange_id = ex_id,
                                       .tick_size = inst->tick_size,
                                       .lot_size = inst->lot_size});
        spdlog::info("  [{}] {} @ {} tick={} lot={}",
                     id,
                     inst->symbol,
                     inst->exchange,
                     inst->tick_size,
                     inst->lot_size);
    }

    spdlog::info("[AS] Trading universe: {} instrument(s)", state_.size());

    if (!md_client_)
        return;

    std::vector<md::MdClient::InstrumentDesc> subs;
    subs.reserve(state_.size());
    for (const auto& [id, st] : state_)
        subs.push_back({id, st.exchange, st.symbol});

    spdlog::info("[AS] Subscribing MD to {} instrument(s)", subs.size());
    md_client_->subscribe(correlation_id_, subs);
}

void AvellanedaStoikovStrategy::on_delta(const refdata::Instrument& inst,
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
                       InstrumentState{.symbol = inst.symbol,
                                       .exchange = inst.exchange,
                                       .exchange_id = ex_id,
                                       .tick_size = inst.tick_size,
                                       .lot_size = inst.lot_size});
        spdlog::info("[AS] Delta ADD {} @ {} tick={} lot={}",
                     inst.symbol,
                     inst.exchange,
                     inst.tick_size,
                     inst.lot_size);

    } else if (update_type == bifrost::protocol::DeltaUpdateType::REMOVE) {
        state_.erase(inst.instrument_id);
        spdlog::info("[AS] Delta REMOVE {} @ {}", inst.symbol, inst.exchange);
    }
}

void AvellanedaStoikovStrategy::on_trade(const bifrost::protocol::MdTrade& tick) {
    auto it = state_.find(tick.instrumentId());
    if (it == state_.end())
        return;

    InstrumentState& st = it->second;
    const uint64_t ts_ns = tick.timestampNs();

    if (st.last_trade_ns > 0 && ts_ns > st.last_trade_ns) {
        const double dt_s = static_cast<double>(ts_ns - st.last_trade_ns) * 1e-9;
        if (dt_s > 0.0) {
            // Instantaneous arrival rate for this side = 1/dt_s.
            // Divide by 2 to split across bid and ask sides (each side gets half
            // the total trade flow in a symmetric market).
            const double arrival_rate = 0.5 / dt_s;
            const double lambda = std::exp(-dt_s / kappa_halflife_s_);
            st.ewma_kappa = lambda * st.ewma_kappa + (1.0 - lambda) * arrival_rate;
            ++st.kappa_ticks;
        }
    }
    st.last_trade_ns = ts_ns;
}

void AvellanedaStoikovStrategy::on_bbo(const bifrost::protocol::MdMarketData& tick) {
    auto it = state_.find(tick.instrumentId());
    if (it == state_.end())
        return;

    InstrumentState& st = it->second;

    const double bid_px = tick.bidPrice();
    const double ask_px = tick.askPrice();
    if (bid_px <= 0.0 || ask_px <= 0.0 || ask_px <= bid_px)
        return;

    const double mid = (bid_px + ask_px) * 0.5;
    const uint64_t ts_ns = tick.timestampNs();

    // ── Update EWMA volatility ─────────────────────────────────────────────
    if (st.session_start_ns == 0)
        st.session_start_ns = ts_ns;

    if (st.last_mid > 0.0 && ts_ns > st.last_tick_ns) {
        const double dt_s = static_cast<double>(ts_ns - st.last_tick_ns) * 1e-9;
        if (dt_s > 0.0) {
            const double log_ret = std::log(mid / st.last_mid);
            const double norm_ret = log_ret / std::sqrt(dt_s);  // per-sqrt-second units
            const double norm_ret_sq = norm_ret * norm_ret;

            // λ = exp(-dt_s / halflife) — recomputed per tick so the decay rate is
            // proportional to elapsed time, not tick count. This makes the EWMA
            // time-consistent regardless of tick arrival rate.
            const double lambda = std::exp(-dt_s / vol_halflife_s_);
            st.ewma_var = lambda * st.ewma_var + (1.0 - lambda) * norm_ret_sq;
            ++st.ewma_ticks;
        }
    }
    st.last_mid = mid;
    st.last_tick_ns = ts_ns;

    // ── Compute AS quotes ──────────────────────────────────────────────────
    // Heimdall encodes quantity at 1e8 fixed-point (same scale as price).
    // Divide by 1e8 to convert raw position to base units (BTC).
    const double net_qty = static_cast<double>(positions_.net_qty(tick.instrumentId(), st.exchange_id)) / 1e8;

    double new_bid{0.0}, new_ask{0.0};
    if (!compute_quotes(st, tick.instrumentId(), net_qty, mid, ts_ns, new_bid, new_ask))
        return;

    maybe_requote(tick.instrumentId(), st, net_qty, mid, new_bid, new_ask);
}

void AvellanedaStoikovStrategy::on_exec_report(const bifrost::protocol::ExecutionReport& rpt) {
    const uint64_t order_id = rpt.orderId();
    const uint64_t instrument_id = rpt.instrumentId();
    const auto status = rpt.status();

    auto inst_it = order_to_instrument_.find(order_id);
    if (inst_it == order_to_instrument_.end())
        return;

    // Use the canonical instrument_id we stored at order placement — the exec
    // report's instrumentId() may be 0 if the gateway doesn't carry canonical IDs.
    const uint64_t canonical_id = inst_it->second;
    auto state_it = state_.find(canonical_id);
    if (state_it == state_.end())
        return;

    InstrumentState& st = state_it->second;

    if (status == ExecStatus::ACKED) {
        spdlog::debug("[AS] ExecReport order_id={} {} {} ACKED", order_id, st.symbol, st.exchange);
    } else if (status == ExecStatus::REJECTED) {
        const auto src = rpt.rejectSource();
        const bool gateway_reject = (src == RejectSource::GATEWAY || src == RejectSource::RISK);
        if (gateway_reject)
            spdlog::error("[AS] ExecReport order_id={} {} {} REJECTED reason={} source={}",
                          order_id,
                          st.symbol,
                          st.exchange,
                          bifrost::protocol::RejectReason::c_str(rpt.rejectReason()),
                          bifrost::protocol::RejectSource::c_str(src));
        else
            spdlog::warn("[AS] ExecReport order_id={} {} {} REJECTED reason={} source={}",
                         order_id,
                         st.symbol,
                         st.exchange,
                         bifrost::protocol::RejectReason::c_str(rpt.rejectReason()),
                         bifrost::protocol::RejectSource::c_str(src));
    } else {
        spdlog::info("[AS] ExecReport order_id={} {} {} status={} filled={:.6f} price={:.2f}",
                     order_id,
                     st.symbol,
                     st.exchange,
                     bifrost::protocol::ExecStatus::c_str(status),
                     static_cast<double>(rpt.filledQty()) / 1e8,
                     static_cast<double>(rpt.price()) / 1e8);
    }

    if (status == ExecStatus::FILLED || status == ExecStatus::PARTIAL) {
        positions_.on_fill(canonical_id, st.exchange_id, rpt.side(), rpt.filledQty(), rpt.price());

        if (const auto pos = positions_.get(canonical_id, st.exchange_id)) {
            spdlog::info("[AS] Position {} @ {}  net_qty={:.6f}  avg_price={:.2f}  rpnl={:.4f}",
                         st.symbol,
                         st.exchange,
                         static_cast<double>(pos->net_qty) / 1e8,
                         pos->avg_price,
                         pos->realized_pnl);
        }
    }

    // Terminal statuses: clear order slot and cancel-pending flags.
    if (status == ExecStatus::FILLED || status == ExecStatus::CANCELLED || status == ExecStatus::REJECTED) {
        if (st.bid_order_id == order_id) {
            st.bid_order_id = 0;
            st.bid_cancel_pending = false;
        } else if (st.ask_order_id == order_id) {
            st.ask_order_id = 0;
            st.ask_cancel_pending = false;
        } else if (st.unwind_order_id == order_id) {
            st.unwind_order_id = 0;
        }
        order_to_instrument_.erase(order_id);
    }
    // PARTIAL: order still live — keep order_id and pending flags unchanged.
    // ACKED:   acknowledged but not yet filled — keep order_id.

    // Exchange-error backoff: consecutive EXCHANGE-sourced rejections trigger
    // increasing cooldowns so we don't flood a broken/unfunded account.
    if (status == ExecStatus::REJECTED && rpt.rejectSource() == RejectSource::EXCHANGE) {
        ++st.consecutive_exchange_errors;
        const uint64_t backoff_s =
            (st.consecutive_exchange_errors == 1) ? 5 :
            (st.consecutive_exchange_errors == 2) ? 15 : 30;
        const uint64_t now_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        st.reject_backoff_until_ns = now_ns + backoff_s * 1'000'000'000ULL;
        spdlog::warn("[AS] Exchange rejection backoff {} @ {}: {}s (consecutive={})",
                     st.symbol, st.exchange, backoff_s, st.consecutive_exchange_errors);
    } else if (status == ExecStatus::ACKED) {
        st.consecutive_exchange_errors = 0;
        st.reject_backoff_until_ns = 0;
    }
}

// ── Private ─────────────────────────────────────────────────────────────────

bool AvellanedaStoikovStrategy::compute_quotes(const InstrumentState& st,
                                               uint64_t instrument_id,
                                               double net_qty,
                                               double mid,
                                               uint64_t timestamp_ns,
                                               double& out_bid,
                                               double& out_ask) const {
    if (st.ewma_ticks < vol_warmup_ticks_)
        return false;
    if (st.ewma_var <= 0.0)
        return false;

    // Remaining session time — clamp to [0, session_duration_s_].
    // After the session ends we keep quoting at the minimum spread (T-t = 0).
    const double elapsed_s = static_cast<double>(timestamp_ns - st.session_start_ns) * 1e-9;
    const double T_minus_t = std::max(0.0, session_duration_s_ - elapsed_s);

    const double sigma_sq = st.ewma_var;
    const double gamma_sigma_sq_T = gamma_ * sigma_sq * T_minus_t;

    // Reservation price (inventory-skewed mid).
    const double reservation = mid - net_qty * gamma_sigma_sq_T;

    // Minimum half-spread: config floor + round-trip maker fee so we never
    // quote a spread that is guaranteed to lose money to commissions.
    // fee_half = maker_bps / 10000 * mid (one leg); both legs = 2x, so each
    // side of the spread must cover at least 1x maker fee.
    double fee_half_spread = 0.0;
    const auto fee_entry = refdata_.fee_cache().get(st.exchange_id, instrument_id, timestamp_ns);
    if (fee_entry) {
        fee_half_spread = (static_cast<double>(fee_entry->maker_bps) / 10000.0) * mid;
    }

    // Use live EWMA κ once warmed up; fall back to config kappa_ before then.
    // Floor at kappa_min_ to prevent ln(1 + γ/κ) → ∞ as κ → 0.
    const double kappa = (st.kappa_ticks >= kappa_warmup_ticks_) ? std::max(kappa_min_, st.ewma_kappa) : kappa_;

    const double min_half_spread = std::max((min_half_spread_bps_ / 10000.0) * mid, fee_half_spread);
    const double half_spread =
        std::max(min_half_spread, gamma_sigma_sq_T / 2.0 + (1.0 / gamma_) * std::log(1.0 + gamma_ / kappa));

    out_bid = reservation - half_spread;
    out_ask = reservation + half_spread;

    spdlog::debug("[AS] quotes σ²={:.2e} κ={:.4f} ({}) half_spread={:.4f} reservation={:.2f}",
                  sigma_sq,
                  kappa,
                  (st.kappa_ticks >= kappa_warmup_ticks_) ? "live" : "fallback",
                  half_spread,
                  reservation);

    return true;
}

void AvellanedaStoikovStrategy::maybe_requote(uint64_t instrument_id,
                                              InstrumentState& st,
                                              double net_qty,
                                              double mid,
                                              double new_bid,
                                              double new_ask) {
    // Honour exchange-error backoff before touching orders on this instrument.
    if (st.reject_backoff_until_ns > 0) {
        const uint64_t now_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        if (now_ns < st.reject_backoff_until_ns)
            return;
        // Backoff expired — clear it and allow quoting to resume.
        st.reject_backoff_until_ns = 0;
        spdlog::info("[AS] Exchange backoff expired for {} @ {}, resuming quotes",
                     st.symbol, st.exchange);
    }

    const bool at_max_long = net_qty >= max_inventory_;
    const bool at_max_short = net_qty <= -max_inventory_;

    // ── Bid side ──────────────────────────────────────────────────────────
    if (st.bid_order_id != 0 && !st.bid_cancel_pending) {
        // Adverse selection guard: cancel if mid has risen significantly since
        // we placed this bid — informed flow is pushing against us.
        const bool adverse =
            st.bid_placed_mid > 0.0 && (mid - st.bid_placed_mid) / st.bid_placed_mid > requote_threshold_;
        // Model drift: modify-in-place if the AS model wants a different price.
        const bool stale =
            st.last_bid_price > 0.0 && std::abs(new_bid - st.last_bid_price) / st.last_bid_price > requote_threshold_;
        // Must cancel if at max inventory (suppress this side entirely).
        if (at_max_long || adverse) {
            // Hard cancel — adverse selection or inventory limit, don't amend.
            if (order_mgr_) {
                spdlog::debug("[AS] Cancel bid order_id={} {} @ {} reason={}",
                              st.bid_order_id,
                              st.symbol,
                              st.exchange,
                              at_max_long ? "max_inv" : "adverse");
                order_mgr_->cancel_order(st.bid_order_id, st.exchange_id, instrument_id);
            }
            st.bid_cancel_pending = true;
        } else if (stale) {
            // Price drift — amend in place to preserve queue position.
            if (order_mgr_) {
                double price = new_bid;
                if (st.tick_size > 0.0)
                    price = std::floor(price / st.tick_size) * st.tick_size;
                const int64_t price_fixed = static_cast<int64_t>(std::round(price * kPriceScale));
                const uint64_t qty_fp = static_cast<uint64_t>(std::round(order_qty_ * 1e8));
                spdlog::debug("[AS] Modify bid order_id={} {} @ {} → {:.6f}",
                              st.bid_order_id,
                              st.symbol,
                              st.exchange,
                              price);
                order_mgr_->modify_order(st.bid_order_id, st.exchange_id, instrument_id, price_fixed, qty_fp);
            }
            st.last_bid_price = new_bid;
            st.bid_placed_mid = mid;
        }
    }

    if (st.bid_order_id == 0 && !st.bid_cancel_pending && !at_max_long) {
        const uint64_t oid =
            send_limit_order(instrument_id, st, bifrost::protocol::OrderSide::BUY, new_bid, order_qty_);
        if (oid != 0) {
            st.bid_order_id = oid;
            st.last_bid_price = new_bid;
            st.bid_placed_mid = mid;
        }
    }

    // ── Ask side ──────────────────────────────────────────────────────────
    if (st.ask_order_id != 0 && !st.ask_cancel_pending) {
        // Adverse selection guard: cancel if mid has dropped since we placed this ask.
        const bool adverse =
            st.ask_placed_mid > 0.0 && (st.ask_placed_mid - mid) / st.ask_placed_mid > requote_threshold_;
        const bool stale =
            st.last_ask_price > 0.0 && std::abs(new_ask - st.last_ask_price) / st.last_ask_price > requote_threshold_;
        if (at_max_short || adverse) {
            if (order_mgr_) {
                spdlog::debug("[AS] Cancel ask order_id={} {} @ {} reason={}",
                              st.ask_order_id,
                              st.symbol,
                              st.exchange,
                              at_max_short ? "max_inv" : "adverse");
                order_mgr_->cancel_order(st.ask_order_id, st.exchange_id, instrument_id);
            }
            st.ask_cancel_pending = true;
        } else if (stale) {
            if (order_mgr_) {
                double price = new_ask;
                if (st.tick_size > 0.0)
                    price = std::ceil(price / st.tick_size) * st.tick_size;
                const int64_t price_fixed = static_cast<int64_t>(std::round(price * kPriceScale));
                const uint64_t qty_fp = static_cast<uint64_t>(std::round(order_qty_ * 1e8));
                spdlog::debug("[AS] Modify ask order_id={} {} @ {} → {:.6f}",
                              st.ask_order_id,
                              st.symbol,
                              st.exchange,
                              price);
                order_mgr_->modify_order(st.ask_order_id, st.exchange_id, instrument_id, price_fixed, qty_fp);
            }
            st.last_ask_price = new_ask;
            st.ask_placed_mid = mid;
        }
    }

    if (st.ask_order_id == 0 && !st.ask_cancel_pending && !at_max_short) {
        const uint64_t oid =
            send_limit_order(instrument_id, st, bifrost::protocol::OrderSide::SELL, new_ask, order_qty_);
        if (oid != 0) {
            st.ask_order_id = oid;
            st.last_ask_price = new_ask;
            st.ask_placed_mid = mid;
        }
    }

    // ── Active inventory unwind ────────────────────────────────────────────
    // When inventory exceeds max_inventory_, send an aggressive LIMIT IOC order
    // to reduce it rather than waiting passively for resting orders to fill.
    if (st.unwind_order_id == 0) {
        if (at_max_long) {
            const uint64_t oid =
                send_unwind_order(instrument_id, st, bifrost::protocol::OrderSide::SELL, mid, order_qty_);
            if (oid != 0)
                st.unwind_order_id = oid;
        } else if (at_max_short) {
            const uint64_t oid =
                send_unwind_order(instrument_id, st, bifrost::protocol::OrderSide::BUY, mid, order_qty_);
            if (oid != 0)
                st.unwind_order_id = oid;
        }
    }
}

uint64_t AvellanedaStoikovStrategy::send_limit_order(uint64_t instrument_id,
                                                     InstrumentState& st,
                                                     bifrost::protocol::OrderSide::Value side,
                                                     double price,
                                                     double qty) {
    const auto vex_it = venue_exec_.find(st.exchange);
    if (vex_it == venue_exec_.end() || !vex_it->second.enabled) {
        spdlog::debug("[AS] Venue {} not enabled — quote suppressed", st.exchange);
        return 0;
    }

    if (!order_mgr_) {
        spdlog::info("[AS] {} {} {} @ {:.6f} (no gateway)",
                     (side == OrderSide::BUY ? "BID" : "ASK"),
                     st.symbol,
                     st.exchange,
                     price);
        return 0;
    }

    // Note: OrderManager rounds BUY up and SELL down. For market-making, we want
    // the opposite (bid floors, ask ceils) to preserve spread width, so pre-round here.
    if (st.tick_size > 0.0) {
        if (side == OrderSide::BUY)
            price = std::floor(price / st.tick_size) * st.tick_size;
        else
            price = std::ceil(price / st.tick_size) * st.tick_size;
    }

    const uint64_t order_id =
        order_mgr_
            ->place_order(instrument_id, st.exchange_id, side, OrderType::POST_ONLY, TimeInForce::GTC, price, qty);
    if (order_id == 0)
        return 0;

    spdlog::info("[AS] {} {} {} @ {:.6f} → order_id={}",
                 (side == OrderSide::BUY ? "BID" : "ASK"),
                 st.symbol,
                 st.exchange,
                 price,
                 order_id);

    order_to_instrument_[order_id] = instrument_id;
    return order_id;
}

uint64_t AvellanedaStoikovStrategy::send_unwind_order(uint64_t instrument_id,
                                                      InstrumentState& st,
                                                      bifrost::protocol::OrderSide::Value side,
                                                      double mid,
                                                      double qty) {
    const auto vex_it = venue_exec_.find(st.exchange);
    if (vex_it == venue_exec_.end() || !vex_it->second.enabled)
        return 0;

    if (!order_mgr_) {
        spdlog::info("[AS] UNWIND {} {} @ {} mid={:.6f} (no gateway)",
                     (side == OrderSide::BUY ? "BUY" : "SELL"),
                     st.symbol,
                     st.exchange,
                     mid);
        return 0;
    }

    // Cross the spread aggressively — 20bps through mid — to ensure immediate fill.
    // Use LIMIT IOC rather than MARKET to avoid OKX SPOT market-buy qty quirks
    // (OKX interprets SPOT market BUY sz as quote currency, not base).
    const double price = (side == OrderSide::BUY) ? mid * 1.002 : mid * 0.998;

    const uint64_t order_id =
        order_mgr_->place_order(instrument_id, st.exchange_id, side, OrderType::LIMIT, TimeInForce::IOC, price, qty);
    if (order_id == 0)
        return 0;

    spdlog::info("[AS] UNWIND {} {} @ {} price={:.6f} mid={:.6f} → order_id={}",
                 (side == OrderSide::BUY ? "BUY" : "SELL"),
                 st.symbol,
                 st.exchange,
                 price,
                 mid,
                 order_id);

    order_to_instrument_[order_id] = instrument_id;
    return order_id;
}

}  // namespace fenrir::strategy
