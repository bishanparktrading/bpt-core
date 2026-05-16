#pragma once

#include "bpt_common/codec/codec.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bpt::md_gateway::messaging {

struct MdServiceHeartbeatMsg {
    uint64_t timestamp_ns;
    uint64_t seq_num;
};

class SbeMdServiceHeartbeatCodec {
public:
    std::span<const std::byte> encode(const MdServiceHeartbeatMsg&, std::span<std::byte> scratch);
    MdServiceHeartbeatMsg       decode(std::span<const std::byte>);

    static constexpr std::size_t kRecommendedScratchSize = 64;
};

static_assert(bpt::common::codec::Codec<SbeMdServiceHeartbeatCodec, MdServiceHeartbeatMsg>);

}  // namespace bpt::md_gateway::messaging
