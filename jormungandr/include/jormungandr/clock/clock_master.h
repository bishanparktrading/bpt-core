#pragma once

#include <chrono>

#include "jormungandr/data/data_loader.h"
#include "jormungandr/exchange/binance_md_server.h"
#include "jormungandr/exchange/okx_md_server.h"
#include "jormungandr/matching/matching_engine.h"
#include "jormungandr/messaging/backtest_ack_subscriber.h"
#include "jormungandr/messaging/backtest_control_publisher.h"
#include "jormungandr/results/results_collector.h"

namespace jormungandr::clock {

// ClockMaster drives the backtest event loop:
//
//   1. Sends BacktestControl(START) to Fenrir and waits for the initial ack.
//   2. For each tick from DataLoader:
//        a. Dispatch the market event to the appropriate WS server.
//        b. Send BacktestControl(START, seq, ts) so Fenrir knows the virtual time.
//        c. Wait for BacktestAck(seq) before advancing to the next tick.
//   3. Sends BacktestControl(STOP) when all data is exhausted.
//
// This implements as-fast-as-possible mode: Jormungandr advances only after
// Fenrir explicitly acknowledges each tick.
class ClockMaster {
public:
    ClockMaster(data::DataLoader& loader, exchange::BinanceMdServer* binance_server,
                exchange::OkxMdServer* okx_server, matching::MatchingEngine* matching_engine,
                results::ResultsCollector* results, messaging::BacktestControlPublisher& ctrl_pub,
                messaging::BacktestAckSubscriber& ack_sub, std::chrono::milliseconds ack_timeout);

    // Runs the simulation to completion.  Throws on ack timeout or
    // unrecoverable Aeron errors.
    void run();

private:
    // Route a single event to the WS server matching its exchange field.
    void dispatch(const data::MarketEvent& event);

    data::DataLoader& loader_;
    exchange::BinanceMdServer* binance_server_;
    exchange::OkxMdServer* okx_server_;
    matching::MatchingEngine* matching_engine_;
    results::ResultsCollector* results_;
    messaging::BacktestControlPublisher& ctrl_pub_;
    messaging::BacktestAckSubscriber& ack_sub_;
    std::chrono::milliseconds ack_timeout_;
};

}  // namespace jormungandr::clock
