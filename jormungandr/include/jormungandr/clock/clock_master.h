#pragma once

#include "jormungandr/data/data_loader.h"
#include "jormungandr/exchange/binance_md_server.h"
#include "jormungandr/exchange/okx_md_server.h"
#include "jormungandr/matching/matching_engine.h"
#include "jormungandr/messaging/backtest_ack_subscriber.h"
#include "jormungandr/messaging/backtest_control_publisher.h"
#include "jormungandr/results/results_collector.h"

#include <chrono>

namespace jormungandr::clock {

// ClockMaster drives the backtest event loop.
//
// Iterates over all events from DataLoader in timestamp order, dispatches
// each to the appropriate mock WS server (Huginn picks them up) and to the
// matching engine and results collector.
//
// When ctrl_pub and ack_sub are provided, ClockMaster gates each tick:
//   1. dispatch event to WS server + matching engine
//   2. send BacktestControl::START(seq, sim_ts) to Fenrir
//   3. block until Fenrir sends BacktestAck(seq)
// This ensures Fenrir processes each tick and any resulting orders before
// Jormungandr advances the simulated clock, eliminating lookahead bias.
class ClockMaster {
public:
    ClockMaster(data::DataLoader& loader,
                exchange::BinanceMdServer* binance_server,
                exchange::OkxMdServer* okx_server,
                matching::MatchingEngine* matching_engine,
                results::ResultsCollector* results,
                messaging::BacktestControlPublisher* ctrl_pub = nullptr,
                messaging::BacktestAckSubscriber* ack_sub = nullptr);

    // Runs the simulation to completion.
    void run();

private:
    void dispatch(const data::MarketEvent& event);

    data::DataLoader& loader_;
    exchange::BinanceMdServer* binance_server_;
    exchange::OkxMdServer* okx_server_;
    matching::MatchingEngine* matching_engine_;
    results::ResultsCollector* results_;
    messaging::BacktestControlPublisher* ctrl_pub_;
    messaging::BacktestAckSubscriber* ack_sub_;

    static constexpr std::chrono::milliseconds kAckTimeout{5000};
};

}  // namespace jormungandr::clock
