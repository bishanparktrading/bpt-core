#pragma once

#include "backtester/messaging/codecs/sbe_backtest_control_codec.h"

#include <messages/BacktestCommand.h>

#include <cstdint>

namespace bpt::backtester::messaging::api {

class BacktestControlPublisher {
public:
    virtual ~BacktestControlPublisher() = default;
    virtual void send(bpt::messages::BacktestCommand::Value cmd, uint64_t tick_seq_num, uint64_t simulation_ts) = 0;
};

}  // namespace bpt::backtester::messaging::api
