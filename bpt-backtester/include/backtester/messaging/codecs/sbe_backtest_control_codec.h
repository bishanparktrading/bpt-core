#pragma once

#include "bpt_common/codec/codec.h"

#include <messages/BacktestCommand.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace bpt::backtester::messaging {

struct BacktestControlMsg {
    bpt::messages::BacktestCommand::Value command;
    uint64_t tick_seq_num;
    uint64_t simulation_ts;
};

class SbeBacktestControlCodec {
public:
    std::span<const std::byte> encode(const BacktestControlMsg&, std::span<std::byte> scratch);
    BacktestControlMsg          decode(std::span<const std::byte>);

    // MessageHeader (8) + BacktestControl block (17) = 25; round up for alignment.
    static constexpr std::size_t kRecommendedScratchSize = 64;
};

static_assert(bpt::common::codec::Codec<SbeBacktestControlCodec, BacktestControlMsg>);

}  // namespace bpt::backtester::messaging
