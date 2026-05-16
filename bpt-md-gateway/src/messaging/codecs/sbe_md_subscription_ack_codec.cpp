#include "md_gateway/messaging/codecs/sbe_md_subscription_ack_codec.h"

#include <messages/MdSubscriptionAck.h>
#include <messages/MessageHeader.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace bpt::md_gateway::messaging {

using bpt::messages::MdSubscriptionAck;
using bpt::messages::MessageHeader;

std::span<const std::byte> SbeMdSubscriptionAckCodec::encode(const MdSubscriptionAckMsg& m,
                                                             std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    MdSubscriptionAck msg;
    msg.wrapAndApplyHeader(buf, 0, scratch.size())
        .correlationId(m.correlation_id)
        .timestampNs(m.timestamp_ns)
        .instrumentId(m.instrument_id)
        .ackStatus(m.ack_status);

    char* ex_field = msg.exchange();
    const std::size_t ex_len = std::min(m.exchange.size(), std::size_t{8});
    std::memcpy(ex_field, m.exchange.data(), ex_len);

    const auto total = MessageHeader::encodedLength() + MdSubscriptionAck::sbeBlockLength();
    return scratch.subspan(0, total);
}

MdSubscriptionAckMsg SbeMdSubscriptionAckCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeMdSubscriptionAckCodec::decode: too short");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != MdSubscriptionAck::sbeTemplateId())
        throw std::runtime_error("SbeMdSubscriptionAckCodec::decode: wrong template id");

    MdSubscriptionAck msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    MdSubscriptionAckMsg out;
    out.correlation_id = msg.correlationId();
    out.timestamp_ns = msg.timestampNs();
    out.instrument_id = msg.instrumentId();
    out.ack_status = msg.ackStatus();
    out.exchange = msg.getExchangeAsString();
    return out;
}

}  // namespace bpt::md_gateway::messaging
