#include "fenrir/strategy/ofi_strategy.h"

#include <bifrost_protocol/DeltaUpdateType.h>
#include <bifrost_protocol/ExchangeId.h>
#include <bifrost_protocol/ExecStatus.h>
#include <bifrost_protocol/InstrumentType.h>
#include <bifrost_protocol/OrderType.h>
#include <bifrost_protocol/RejectReason.h>
#include <bifrost_protocol/RejectSource.h>
#include <bifrost_protocol/TimeInForce.h>

#include <algorithm>
#include <cmath>

using bifrost::protocol::ExchangeId;
using bifrost::protocol::ExecStatus;
using bifrost::protocol::OrderSide;
using bifrost::protocol::OrderType;
using bifrost::protocol::TimeInForce;

namespace fenrir::strategy {

static constexpr double kPriceScale = 1e8;
static constexpr double kQtyScale = 1e8;

// Aggressiveness for taker IOC limits — cross the book by this many bps.
static constexpr double kAggressBps = 1.0;

// ── Constructor ──────────────────────────────────────────────────────────────

OFIStrategy::OFIStrategy(uint64_t correlation_id,
                          const config::StrategyConfig& cfg,
                          refdata::RefdataClient& refdata,
                          md::MdClient* md,
                          order::OrderManager* order_mgr)
    : correlation_id_(correlation_id),
      book_levels_(static_cast<int>(cfg.params["book_levels"].value<int64_t>().value_or(5))),
      ofi_window_ns_(static_cast<uint64_t>(cfg.params["ofi_window_ms"].value<double>().value_or(1000.0) * 1e6)),
      entry_threshold_(cfg.params["entry_threshold"].value<double>().value_or(0.35)),
      exit_threshold_(cfg.params["exit_threshold"].value<double>().value_or(0.15)),
      stop_bps_(cfg.params["stop_bps"].value<double>().value_or(8.0)),
      target_bps_(cfg.params["target_bps"].value<double>().value_or(12.0)),
      max_hold_ns_(static_cast<uint64_t>(cfg.params["max_hold_seconds"].value<double>().value_or(30.0) * 1e9)),
      cooldown_ticks_(static_cast<int>(cfg.params["cooldown_ticks"].value<int64_t>().value_or(20))),
      qty_usd_(cfg.params["qty_usd"].value<double>().value_or(200.0)),
      max_spread_bps_(cfg.params["max_spread_bps"].value<double>().value_or(5.0)),
      order_book_depth_(static_cast<uint8_t>(cfg.params["order_book_depth"].value<int64_t>().value_or(5))),
      instruments_(cfg.instruments),
      md_exchanges_(cfg.md_exchanges),
      venue_exec_(cfg.venue_exec),
      refdata_(refdata),
      md_client_(md),
      order_mgr_(order_mgr) {
    ygg::log::info("[OFI] levels={} window={}ms entry={:.2f} exit={:.2f}",
                   book_levels_,
                   ofi_window_ns_ / 1'000'000,
                   entry_threshold_,
                   exit_threshold_);
    ygg::log::info("[OFI] stop={:.1f}bps target={:.1f}bps max_hold={:.0f}s cooldown={}ticks",
                   stop_bps_,
                   target_bps_,
                   max_hold_ns_ / 1e9,
                   cooldown_ticks_);
    ygg::log::info("[OFI] qty_usd={:.0f} max_spread={:.1f}bps depth={}",
                   qty_usd_,
                   max_spread_bps_,
                   order_book_depth_);
}

// ── IStrategy lifecycle ──────────────────────────────────────────────────────

void OFIStrategy::start() {
    std::vector<refdata::RefdataClient::CanonicalFilter> filters;
    for (const auto& sym : instruments_) {
        if (auto parsed = CanonicalResolver::parse(sym)) {
            const auto sbe_type = [&]() {
                using T = refdata::InstrumentType;
                using S = bifrost::protocol::InstrumentType;
                switch (parsed->type) {
                    case T::SPOT:       return S::SPOT;
                    case T::PERPETUAL:  return S::PERPETUAL;
                    case T::FUTURE:     return S::FUTURE;
                    case T::OPTION:     return S::OPTION;
                    default:            return S::NULL_VALUE;
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

void OFIStrategy::on_snapshot(const refdata::InstrumentCache& cache) {
    if (!state_.empty()) {
        ygg::log::debug("[OFI] Ignoring duplicate snapshot ({} instruments)", cache.size());
        return;
    }

    const OFICalculator::Config ofi_cfg{book_levels_, ofi_window_ns_};

    const auto all_ids = CanonicalResolver::resolve(cache, instruments_, md_exchanges_);
    for (uint64_t id : all_ids) {
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
        else if (inst->exchange == "DERIBIT")
            ex_id = ExchangeId::DERIBIT;

        InstrumentState st(ofi_cfg);
        st.instrument_id = id;
        st.symbol = inst->symbol;
        st.exchange = inst->exchange;
        st.exchange_id = ex_id;
        st.tick_size = inst->tick_size;
        st.lot_size = inst->lot_size;

        ygg::log::info("[OFI] Instrument [{}] {} @ {} tick={} lot={}",
                       id, inst->symbol, inst->exchange, inst->tick_size, inst->lot_size);
        state_.emplace(id, std::move(st));
    }
    ygg::log::info("[OFI] Resolved {} instrument(s)", state_.size());

    if (!md_client_)
        return;
    std::vector<md::MdClient::InstrumentDesc> subs;
    for (const auto& [id, st] : state_)
        subs.push_back({id, st.exchange, st.symbol, order_book_depth_});
    md_client_->subscribe(correlation_id_, subs);
}

void OFIStrategy::on_delta(const refdata::Instrument& /*inst*/,
                            bifrost::protocol::DeltaUpdateType::Value /*type*/) {}

void OFIStrategy::on_trade(const bifrost::protocol::MdTrade& /*tick*/) {}

// ── Market data ──────────────────────────────────────────────────────────────

void OFIStrategy::on_bbo(const bifrost::protocol::MdMarketData& tick) {
    auto it = state_.find(tick.instrumentId());
    if (it == state_.end())
        return;

    InstrumentState& st = it->second;
    const double bid = tick.bidPrice();
    const double ask = tick.askPrice();
    if (bid <= 0.0 || ask <= 0.0 || ask <= bid)
        return;

    st.bid = bid;
    st.ask = ask;
    st.last_bbo_ns = tick.timestampNs();

    // Time-based exit: check even on BBO ticks so positions don't overrun
    // max_hold while waiting for the next book update.
    if (st.pos != Position::FLAT && max_hold_ns_ > 0 &&
        st.last_bbo_ns - st.entry_ns > max_hold_ns_) {
        ygg::log::info("[OFI] {} time_stop ({}s) — exiting {}",
                       st.symbol,
                       (st.last_bbo_ns - st.entry_ns) / 1'000'000'000ULL,
                       st.pos == Position::LONG ? "LONG" : "SHORT");
        const auto exit_side = (st.pos == Position::LONG) ? OrderSide::SELL : OrderSide::BUY;
        fire_order(st, exit_side, qty_usd_);
        st.pos = Position::FLAT;
        st.cooldown_ticks_remaining = cooldown_ticks_;
    }
}

void OFIStrategy::on_order_book(const bifrost::protocol::MdOrderBook& book) {
    auto it = state_.find(book.instrumentId());
    if (it == state_.end())
        return;
    InstrumentState& st = it->second;

    // SBE group iterators share the parent position — bids must be fully
    // consumed before calling asks().
    auto& mutable_book = const_cast<bifrost::protocol::MdOrderBook&>(book);

    std::vector<OFICalculator::Level> bids;
    std::vector<OFICalculator::Level> asks;
    bids.reserve(static_cast<size_t>(book_levels_));
    asks.reserve(static_cast<size_t>(book_levels_));

    auto& bids_grp = mutable_book.bids();
    while (bids_grp.hasNext()) {
        auto& lvl = bids_grp.next();
        bids.emplace_back(lvl.price(), static_cast<double>(lvl.qty()) / kQtyScale);
    }
    auto& asks_grp = mutable_book.asks();
    while (asks_grp.hasNext()) {
        auto& lvl = asks_grp.next();
        asks.emplace_back(lvl.price(), static_cast<double>(lvl.qty()) / kQtyScale);
    }

    const uint64_t now_ns = book.timestampNs();
    const double ofi = st.ofi.update(bids, asks, now_ns);

    if (st.cooldown_ticks_remaining > 0)
        --st.cooldown_ticks_remaining;

    // Bail on any outstanding order — one in flight at a time.
    if (st.active_order_id != 0)
        return;

    if (st.pos == Position::FLAT) {
        try_enter(st, ofi, now_ns);
    } else {
        try_exit(st, ofi, now_ns);
    }
}

// ── Entry ───────────────────────────────────────────────────────────────────

void OFIStrategy::try_enter(InstrumentState& st, double ofi_value, uint64_t now_ns) {
    if (st.cooldown_ticks_remaining > 0)
        return;
    if (!st.ofi.is_warm())
        return;
    if (st.bid <= 0.0 || st.ask <= 0.0)
        return;

    const double mid = (st.bid + st.ask) * 0.5;
    const double spread_bps = (st.ask - st.bid) / mid * 1e4;
    if (spread_bps > max_spread_bps_)
        return;

    if (ofi_value > entry_threshold_) {
        ygg::log::info("[OFI] {} ENTER LONG ofi={:.3f} mid={:.4f} spread={:.1f}bps",
                       st.symbol, ofi_value, mid, spread_bps);
        fire_order(st, OrderSide::BUY, qty_usd_);
        st.pos = Position::LONG;
        st.entry_price = mid;
        st.entry_ns = now_ns;
    } else if (ofi_value < -entry_threshold_) {
        ygg::log::info("[OFI] {} ENTER SHORT ofi={:.3f} mid={:.4f} spread={:.1f}bps",
                       st.symbol, ofi_value, mid, spread_bps);
        fire_order(st, OrderSide::SELL, qty_usd_);
        st.pos = Position::SHORT;
        st.entry_price = mid;
        st.entry_ns = now_ns;
    }
}

// ── Exit ────────────────────────────────────────────────────────────────────

void OFIStrategy::try_exit(InstrumentState& st, double ofi_value, uint64_t now_ns) {
    if (st.bid <= 0.0 || st.ask <= 0.0)
        return;
    const double mid = (st.bid + st.ask) * 0.5;

    const double move_bps = (mid - st.entry_price) / st.entry_price * 1e4;
    const bool is_long = (st.pos == Position::LONG);
    const double pnl_bps = is_long ? move_bps : -move_bps;

    const char* reason = nullptr;
    if (pnl_bps >= target_bps_)
        reason = "target";
    else if (pnl_bps <= -stop_bps_)
        reason = "stop";
    else if (max_hold_ns_ > 0 && now_ns - st.entry_ns > max_hold_ns_)
        reason = "time_stop";
    else if (is_long && ofi_value < -exit_threshold_)
        reason = "signal_flip";
    else if (!is_long && ofi_value > exit_threshold_)
        reason = "signal_flip";

    if (!reason)
        return;

    ygg::log::info("[OFI] {} EXIT {} reason={} pnl={:.1f}bps ofi={:.3f}",
                   st.symbol, is_long ? "LONG" : "SHORT", reason, pnl_bps, ofi_value);

    const auto exit_side = is_long ? OrderSide::SELL : OrderSide::BUY;
    fire_order(st, exit_side, qty_usd_);
    st.pos = Position::FLAT;
    st.cooldown_ticks_remaining = cooldown_ticks_;
}

// ── Order submission ────────────────────────────────────────────────────────

void OFIStrategy::fire_order(InstrumentState& st,
                               bifrost::protocol::OrderSide::Value side,
                               double qty_usd) {
    if (!order_mgr_) {
        ygg::log::warn("[OFI] {} order_mgr null — dropping order", st.symbol);
        return;
    }

    const double mid = (st.bid + st.ask) * 0.5;
    if (mid <= 0.0)
        return;

    const double qty = qty_usd / mid;

    // Aggressive LIMIT IOC — cross the spread so it takes immediately.
    const double cross = mid * (kAggressBps / 1e4);
    const double price = (side == OrderSide::BUY) ? (st.ask + cross) : (st.bid - cross);

    const uint64_t oid = order_mgr_->place_order(st.instrument_id, st.exchange_id, side,
                                                   OrderType::LIMIT, TimeInForce::IOC,
                                                   price, qty);
    if (oid == 0) {
        ygg::log::warn("[OFI] {} place_order rejected — preflight failed", st.symbol);
        return;
    }
    st.active_order_id = oid;
    order_to_instrument_[oid] = st.instrument_id;
}

// ── Execution reports ───────────────────────────────────────────────────────

void OFIStrategy::on_exec_report(const bifrost::protocol::ExecutionReport& rpt) {
    const uint64_t order_id = rpt.orderId();
    auto inst_it = order_to_instrument_.find(order_id);
    if (inst_it == order_to_instrument_.end())
        return;

    auto st_it = state_.find(inst_it->second);
    if (st_it == state_.end())
        return;
    InstrumentState& st = st_it->second;

    const auto status = rpt.status();
    if (status == ExecStatus::ACKED)
        return;

    if (status == ExecStatus::REJECTED) {
        ygg::log::warn("[OFI] {} order_id={} REJECTED reason={} source={}",
                       st.symbol, order_id,
                       bifrost::protocol::RejectReason::c_str(rpt.rejectReason()),
                       bifrost::protocol::RejectSource::c_str(rpt.rejectSource()));
    } else {
        ygg::log::info("[OFI] {} order_id={} {} filled={:.6f}@{:.4f}",
                       st.symbol, order_id,
                       bifrost::protocol::ExecStatus::c_str(status),
                       static_cast<double>(rpt.filledQty()) / kQtyScale,
                       static_cast<double>(rpt.price()) / kPriceScale);
    }

    const bool is_terminal =
        (status == ExecStatus::FILLED || status == ExecStatus::CANCELLED || status == ExecStatus::REJECTED);
    if (!is_terminal)
        return;

    // IOC order didn't fill — treat the attempted leg as cancelled.
    // If this was an entry, roll back to FLAT; cooldown to avoid chase.
    // If this was an exit, the position is still on — clear active_order_id
    // so the next tick can retry.
    if (status != ExecStatus::FILLED && order_id == st.active_order_id) {
        ygg::log::info("[OFI] {} order_id={} did not fill — reverting state", st.symbol, order_id);
        st.pos = Position::FLAT;
        st.cooldown_ticks_remaining = cooldown_ticks_;
    }

    if (order_id == st.active_order_id)
        st.active_order_id = 0;
    order_to_instrument_.erase(order_id);
}

}  // namespace fenrir::strategy
