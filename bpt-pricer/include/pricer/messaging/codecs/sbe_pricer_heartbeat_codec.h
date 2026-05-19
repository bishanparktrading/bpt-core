#pragma once

/// @file
/// SBE codec for the pricer's PricerHeartbeat status message.
///
/// One codec per message type — keeps each statically testable in
/// isolation and lets a future non-Aeron status transport reuse just
/// the codec it needs.

#include "bpt_common/codec/codec.h"

#include <cstdint>
#include <span>

namespace bpt::pricer::messaging {

/// Domain shape for PricerHeartbeat. Currently a 1:1 mirror of the SBE
/// fields; lives here rather than carrying SBE types into the strategy
/// API so future schema additions don't ripple.
struct PricerHeartbeatMsg {
    uint64_t timestamp_ns;
    uint64_t seq_num;
};

class SbePricerHeartbeatCodec {
public:
    std::span<const std::byte> encode(const PricerHeartbeatMsg&, std::span<std::byte> scratch);
    PricerHeartbeatMsg decode(std::span<const std::byte>);

    static constexpr std::size_t kRecommendedScratchSize = 128;
};

static_assert(bpt::common::codec::Codec<SbePricerHeartbeatCodec, PricerHeartbeatMsg>);

}  // namespace bpt::pricer::messaging
