#pragma once

#include "bpt_common/codec/codec.h"

#include <messages/AckStatus.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace bpt::md_gateway::messaging {

struct MdSubscriptionAckMsg {
    uint64_t correlation_id;
    uint64_t timestamp_ns;
    uint64_t instrument_id;
    bpt::messages::AckStatus::Value ack_status;
    std::string exchange;  ///< ≤ 8 chars; padded/truncated to fit
};

class SbeMdSubscriptionAckCodec {
public:
    std::span<const std::byte> encode(const MdSubscriptionAckMsg&, std::span<std::byte> scratch);
    MdSubscriptionAckMsg        decode(std::span<const std::byte>);

    static constexpr std::size_t kRecommendedScratchSize = 128;
};

static_assert(bpt::common::codec::Codec<SbeMdSubscriptionAckCodec, MdSubscriptionAckMsg>);

}  // namespace bpt::md_gateway::messaging
