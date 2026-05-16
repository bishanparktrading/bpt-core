#include "order_gateway/messaging/codecs/sbe_order_gateway_heartbeat_codec.h"

#include <messages/MessageHeader.h>
#include <messages/OrderGatewayHeartbeat.h>

#include <cstring>
#include <stdexcept>

namespace bpt::order_gateway::messaging {

using bpt::messages::MessageHeader;
using bpt::messages::OrderGatewayHeartbeat;

std::span<const std::byte> SbeOrderGatewayHeartbeatCodec::encode(const OrderGatewayHeartbeatMsg& m,
                                                                 std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    OrderGatewayHeartbeat msg;
    msg.wrapAndApplyHeader(buf, 0, scratch.size())
        .serviceId(m.service_id)
        .timestampNs(m.timestamp_ns)
        .ordersInFlight(m.orders_in_flight)
        .exchangeStatus(m.exchange_status);

    const auto total = MessageHeader::encodedLength() + OrderGatewayHeartbeat::sbeBlockLength();
    return scratch.subspan(0, total);
}

OrderGatewayHeartbeatMsg SbeOrderGatewayHeartbeatCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeOrderGatewayHeartbeatCodec::decode: too short");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != OrderGatewayHeartbeat::sbeTemplateId())
        throw std::runtime_error("SbeOrderGatewayHeartbeatCodec::decode: wrong template id");

    OrderGatewayHeartbeat msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    return OrderGatewayHeartbeatMsg{msg.serviceId(), msg.timestampNs(), msg.ordersInFlight(), msg.exchangeStatus()};
}

}  // namespace bpt::order_gateway::messaging
