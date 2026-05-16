#pragma once

#include "backtester/messaging/codecs/sbe_backtest_control_codec.h"
#include "backtester/messaging/publishers/api/backtest_control_publisher.h"

#include <Aeron.h>

#include <cstdint>
#include <memory>

namespace bpt::backtester::messaging::aeron {

class BacktestControlPublisher : public api::BacktestControlPublisher {
public:
    explicit BacktestControlPublisher(std::shared_ptr<::aeron::Publication> pub);

    void send(bpt::messages::BacktestCommand::Value cmd, uint64_t tick_seq_num, uint64_t simulation_ts) override;

private:
    std::shared_ptr<::aeron::Publication> pub_;
    SbeBacktestControlCodec               codec_;
};

}  // namespace bpt::backtester::messaging::aeron
