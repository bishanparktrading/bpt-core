#pragma once

#include "bpt_common/codec/codec.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bpt::backtester::messaging {

struct BacktestAckMsg {
    uint64_t tick_seq_num;
    uint64_t simulation_ts;
};

class SbeBacktestAckCodec {
public:
    std::span<const std::byte> encode(const BacktestAckMsg&, std::span<std::byte> scratch);
    BacktestAckMsg decode(std::span<const std::byte>);

    static constexpr std::size_t kRecommendedScratchSize = 64;
};

static_assert(bpt::common::codec::Codec<SbeBacktestAckCodec, BacktestAckMsg>);

}  // namespace bpt::backtester::messaging
