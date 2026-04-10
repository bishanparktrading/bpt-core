#pragma once

#include <Aeron.h>

#include <memory>
#include <optional>

#include "jormungandr/clock/clock_master.h"
#include "jormungandr/config/settings.h"
#include "jormungandr/data/data_loader.h"
#include "jormungandr/exchange/binance_md_server.h"
#include "jormungandr/exchange/binance_order_server.h"
#include "jormungandr/exchange/okx_md_server.h"
#include "jormungandr/exchange/okx_order_server.h"
#include "jormungandr/matching/matching_engine.h"
#include "jormungandr/messaging/backtest_ack_subscriber.h"
#include "jormungandr/messaging/backtest_control_publisher.h"
#include "jormungandr/results/results_collector.h"

namespace jormungandr {

class JormungandrApp {
public:
    JormungandrApp(config::Settings settings, std::shared_ptr<aeron::Aeron> aeron);

    // Blocking run loop — feeds ticks until the backtest window is exhausted or a signal fires.
    void run();

private:
    config::Settings settings_;

    // Components are initialised in constructor order; run() drives them.
    std::unique_ptr<data::DataLoader> loader_;
    std::unique_ptr<exchange::BinanceMdServer> binance_md_server_;
    std::unique_ptr<exchange::OkxMdServer> okx_md_server_;
    std::unique_ptr<matching::MatchingEngine> matching_engine_;
    std::unique_ptr<exchange::BinanceOrderServer> binance_order_server_;
    std::unique_ptr<exchange::OkxOrderServer> okx_order_server_;
    std::unique_ptr<results::ResultsCollector> results_;
    std::unique_ptr<messaging::BacktestControlPublisher> ctrl_pub_;
    std::unique_ptr<messaging::BacktestAckSubscriber> ack_sub_;
    std::unique_ptr<clock::ClockMaster> clock_master_;
};

}  // namespace jormungandr
