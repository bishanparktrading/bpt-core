#pragma once

#include "bpt_common/codec/codec.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace bpt::order_gateway::messaging {

struct OrderGatewayHeartbeatMsg {
    uint8_t  service_id;
    uint64_t timestamp_ns;
    uint16_t orders_in_flight;
    uint8_t  exchange_status;  ///< bitmask of which adapters are healthy
};

class SbeOrderGatewayHeartbeatCodec {
public:
    std::span<const std::byte> encode(const OrderGatewayHeartbeatMsg&, std::span<std::byte> scratch);
    OrderGatewayHeartbeatMsg    decode(std::span<const std::byte>);

    static constexpr std::size_t kRecommendedScratchSize = 64;
};

static_assert(bpt::common::codec::Codec<SbeOrderGatewayHeartbeatCodec, OrderGatewayHeartbeatMsg>);

}  // namespace bpt::order_gateway::messaging
