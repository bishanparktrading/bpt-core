#include "jormungandr/clock/clock_master.h"

#include <bifrost_protocol/BacktestCommand.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>

#include "jormungandr/data/orderbook_record.h"
#include "jormungandr/data/trade_record.h"
#include "jormungandr/exchange/okx_md_server.h"
#include "jormungandr/results/results_collector.h"

namespace jormungandr::clock {

using bifrost::protocol::BacktestCommand;

ClockMaster::ClockMaster(data::DataLoader& loader, exchange::BinanceMdServer* binance_server,
                         exchange::OkxMdServer* okx_server,
                         matching::MatchingEngine* matching_engine,
                         results::ResultsCollector* results,
                         messaging::BacktestControlPublisher& ctrl_pub,
                         messaging::BacktestAckSubscriber& ack_sub,
                         std::chrono::milliseconds ack_timeout)
    : loader_(loader),
      binance_server_(binance_server),
      okx_server_(okx_server),
      matching_engine_(matching_engine),
      results_(results),
      ctrl_pub_(ctrl_pub),
      ack_sub_(ack_sub),
      ack_timeout_(ack_timeout) {}

void ClockMaster::run() {
    // ── Phase 1: handshake ─────────────────────────────────────────────────
    spdlog::info("[ClockMaster] Sending START to Fenrir");
    ctrl_pub_.send(BacktestCommand::Value::START, 0, 0);

    if (!ack_sub_.wait_for(0, ack_timeout_)) {
        throw std::runtime_error("[ClockMaster] Fenrir did not ack START within " +
                                 std::to_string(ack_timeout_.count()) + " ms");
    }
    spdlog::info("[ClockMaster] Fenrir ready, driving ticks");

    // ── Phase 2: tick loop ─────────────────────────────────────────────────
    uint64_t seq = 0;
    uint64_t last_ts = 0;

    while (auto event = loader_.next()) {
        dispatch(*event);
        last_ts = event->timestamp_ns;

        ++seq;
        ctrl_pub_.send(BacktestCommand::Value::START, seq, last_ts);

        if (!ack_sub_.wait_for(seq, ack_timeout_)) {
            throw std::runtime_error("[ClockMaster] Ack timeout at tick " + std::to_string(seq) +
                                     ", simulationTs=" + std::to_string(last_ts));
        }

        if (seq % 100'000 == 0)
            spdlog::debug("[ClockMaster] {} ticks processed, last_ts={}", seq, last_ts);
    }

    // ── Phase 3: shutdown ──────────────────────────────────────────────────
    spdlog::info("[ClockMaster] Data exhausted after {} ticks. Sending STOP.", seq);
    ctrl_pub_.send(BacktestCommand::Value::STOP, seq, last_ts);
}

void ClockMaster::dispatch(const data::MarketEvent& event) {
    const std::string& exchange = (event.type == data::MarketEvent::Type::TRADE)
                                      ? std::get<data::TradeRecord>(event.payload).exchange
                                      : std::get<data::OrderBookRecord>(event.payload).exchange;

    if (exchange == "BINANCE") {
        if (binance_server_) binance_server_->push(event);
    } else if (exchange == "OKX") {
        if (okx_server_) okx_server_->push(event);
    } else {
        // HYPERLIQUID server will be wired in once that adapter is built.
        spdlog::warn("[ClockMaster] No WS server for exchange '{}' — event dropped", exchange);
    }

    // Always update the matching engine and results collector, regardless of exchange.
    if (matching_engine_) matching_engine_->on_market_event(event);
    if (results_) results_->on_market_event(event);
}

}  // namespace jormungandr::clock
