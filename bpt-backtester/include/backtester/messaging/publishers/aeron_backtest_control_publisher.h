#pragma once

#include "backtester/messaging/codecs/sbe_backtest_control_codec.h"
#include "backtester/messaging/publishers/i_backtest_control_publisher.h"

#include <Aeron.h>

#include <cstdint>
#include <memory>

namespace bpt::backtester::messaging {

class AeronBacktestControlPublisher : public IBacktestControlPublisher {
public:
    explicit AeronBacktestControlPublisher(std::shared_ptr<aeron::Publication> pub);

    void send(bpt::messages::BacktestCommand::Value cmd, uint64_t tick_seq_num, uint64_t simulation_ts) override;

private:
    std::shared_ptr<aeron::Publication> pub_;
    SbeBacktestControlCodec             codec_;
};

}  // namespace bpt::backtester::messaging
