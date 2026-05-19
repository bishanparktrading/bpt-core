#pragma once

/// \file
/// Thin Handler the templated `InProcessMdClient<HarnessHandler>` dispatches
/// into. Just forwards to the harness's strategy; the no-op heartbeat
/// satisfies the Handler shape (heartbeats aren't meaningful in
/// deterministic backtest replay).
///
/// Lives in its own header so `harness_md_publisher.h` (which holds an
/// `InProcessMdClient<HarnessHandler>&`) can include it without dragging
/// the rest of StrategyHarness.

#include "strategy/strategy/i_strategy.h"

#include <messages/MdMarketData.h>
#include <messages/MdOrderBook.h>
#include <messages/MdTrade.h>

namespace bpt::backtester::harness {

struct HarnessHandler {
    bpt::strategy::strategy::IStrategy* strategy{nullptr};
    void on_bbo(const bpt::messages::MdMarketData& t) { strategy->on_bbo(t); }
    void on_trade(const bpt::messages::MdTrade& t) { strategy->on_trade(t); }
    void on_order_book(const bpt::messages::MdOrderBook& b) { strategy->on_order_book(b); }
    void on_md_service_heartbeat() {}
};

}  // namespace bpt::backtester::harness
