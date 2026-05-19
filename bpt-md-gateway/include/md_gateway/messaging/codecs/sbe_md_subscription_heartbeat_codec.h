#pragma once

#include "bpt_common/codec/codec.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bpt::md_gateway::messaging {

struct MdSubscriptionHeartbeatMsg {
    uint64_t timestamp_ns;
    uint64_t instrument_id;
    uint64_t seq_num;
};

class SbeMdSubscriptionHeartbeatCodec {
public:
    std::span<const std::byte> encode(const MdSubscriptionHeartbeatMsg&, std::span<std::byte> scratch);
    MdSubscriptionHeartbeatMsg decode(std::span<const std::byte>);

    static constexpr std::size_t kRecommendedScratchSize = 64;
};

static_assert(bpt::common::codec::Codec<SbeMdSubscriptionHeartbeatCodec, MdSubscriptionHeartbeatMsg>);

}  // namespace bpt::md_gateway::messaging
