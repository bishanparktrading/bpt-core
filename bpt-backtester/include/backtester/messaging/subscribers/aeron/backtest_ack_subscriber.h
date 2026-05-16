#pragma once

#include "backtester/messaging/codecs/sbe_backtest_ack_codec.h"
#include "backtester/messaging/subscribers/api/backtest_ack_subscriber.h"

#include <Aeron.h>

#include <chrono>
#include <cstdint>
#include <memory>

namespace bpt::backtester::messaging::aeron {

class BacktestAckSubscriber : public api::BacktestAckSubscriber {
public:
    explicit BacktestAckSubscriber(std::shared_ptr<::aeron::Subscription> sub);

    bool wait_for(uint64_t expected_seq, std::chrono::milliseconds timeout) override;

private:
    std::shared_ptr<::aeron::Subscription> sub_;
    SbeBacktestAckCodec                    codec_;
};

}  // namespace bpt::backtester::messaging::aeron
