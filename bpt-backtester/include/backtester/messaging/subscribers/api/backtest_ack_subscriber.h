#pragma once

#include <chrono>
#include <cstdint>

namespace bpt::backtester::messaging::api {

class BacktestAckSubscriber {
public:
    virtual ~BacktestAckSubscriber() = default;

    /// Blocks until a BacktestAck with the given tick_seq arrives or the
    /// timeout elapses.
    virtual bool wait_for(uint64_t expected_seq, std::chrono::milliseconds timeout) = 0;
};

}  // namespace bpt::backtester::messaging::api
