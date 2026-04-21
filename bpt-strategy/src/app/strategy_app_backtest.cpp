#include "strategy/app/strategy_app.h"

#include <messages/BacktestCommand.h>

#include <chrono>
#include <thread>
#include <bpt_common/signal.h>

// Backtest-mode tick-gating loop. Extracted from strategy_app.cpp —
// the live and backtest main loops share no polling state (backtest
// is driven by explicit TICK commands from bpt-backtester, live runs
// a busy poll loop), so keeping them separate keeps each one readable
// without cross-referencing the other.

using namespace std::chrono_literals;

namespace bpt::strategy {

void StrategyApp::run_backtest_loop() {
    bpt::common::log::info("Backtest: strategy ready — entering tick-gating loop");

    bool stop_received = false;

    backtest_client_->on_control =
        [this, &stop_received](bpt::messages::BacktestCommand::Value cmd, uint64_t seq, uint64_t sim_ts) {
            using bpt::messages::BacktestCommand;

            if (cmd == BacktestCommand::START) {
                if (seq == 0) {
                    // Initial handshake — Backtester is ready to start ticking.
                    bpt::common::log::info("Backtest handshake received, acking");
                    backtest_client_->send_ack(0, 0);
                } else {
                    // Normal tick — drain MD and execution reports for up to 10 ms,
                    // then signal Backtester to advance to the next tick.
                    auto deadline = std::chrono::steady_clock::now() + 10ms;
                    int drained = 0;
                    while (std::chrono::steady_clock::now() < deadline) {
                        int f = 0;
                        if (md_client_)
                            f += md_client_->poll();
                        if (order_gw_)
                            f += order_gw_->poll();
                        drained += f;
                        if (f == 0 && drained > 0)
                            break;  // drained everything, stop early
                        if (f == 0)
                            std::this_thread::sleep_for(100us);
                    }
                    backtest_client_->send_ack(seq, sim_ts);
                }
            } else if (cmd == BacktestCommand::STOP) {
                bpt::common::log::info("Backtest STOP received — halting");
                stop_received = true;
            }
        };

    while (!stop_received && bpt::common::signal::is_running()) {
        int frags = backtest_client_->poll();
        if (frags == 0)
            std::this_thread::sleep_for(10us);
    }
}

}  // namespace bpt::strategy
