#pragma once

/// @file
/// SBE codec for the pricer's PricerReady status message — emitted
/// once after initial surface population so downstream consumers can
/// gate on it before depending on any surface data.

#include "bpt_common/codec/codec.h"

#include <cstdint>
#include <span>

namespace bpt::pricer::messaging {

struct PricerReadyMsg {
    uint64_t timestamp_ns;
    uint8_t  exchanges_loaded;   ///< bitmask
    uint16_t underlying_count;
    uint32_t point_count;
};

class SbePricerReadyCodec {
public:
    std::span<const std::byte> encode(const PricerReadyMsg&, std::span<std::byte> scratch);
    PricerReadyMsg              decode(std::span<const std::byte>);

    static constexpr std::size_t kRecommendedScratchSize = 128;
};

static_assert(bpt::common::codec::Codec<SbePricerReadyCodec, PricerReadyMsg>);

}  // namespace bpt::pricer::messaging
