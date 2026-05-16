#pragma once

/// @file
/// POD memcpy codec for the radar MarketColor stream. Like
/// PodToxicityCodec in analytics, the wire format is the raw POD
/// struct shipped as bytes — this wrapper makes that explicit and
/// gives the codec abstraction uniform shape across services even when
/// no SBE is involved.

#include "bpt_common/codec/codec.h"
#include "radar/messaging/market_color.h"

#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>

namespace bpt::radar::messaging {

class PodMarketColorCodec {
public:
    std::span<const std::byte> encode(const MarketColor& c, std::span<std::byte> scratch) {
        if (scratch.size() < sizeof(MarketColor))
            throw std::runtime_error("PodMarketColorCodec::encode: scratch too small");
        std::memcpy(scratch.data(), &c, sizeof(MarketColor));
        return scratch.subspan(0, sizeof(MarketColor));
    }

    MarketColor decode(std::span<const std::byte> bytes) {
        if (bytes.size() != sizeof(MarketColor))
            throw std::runtime_error("PodMarketColorCodec::decode: wrong size");
        MarketColor c;
        std::memcpy(&c, bytes.data(), sizeof(MarketColor));
        return c;
    }

    static constexpr std::size_t kRecommendedScratchSize = sizeof(MarketColor);
};

static_assert(bpt::common::codec::Codec<PodMarketColorCodec, MarketColor>);

}  // namespace bpt::radar::messaging
