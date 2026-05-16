#include "md_gateway/messaging/codecs/sbe_md_subscription_heartbeat_codec.h"

#include <messages/MdSubscriptionHeartbeat.h>
#include <messages/MessageHeader.h>

#include <cstring>
#include <stdexcept>

namespace bpt::md_gateway::messaging {

using bpt::messages::MdSubscriptionHeartbeat;
using bpt::messages::MessageHeader;

std::span<const std::byte> SbeMdSubscriptionHeartbeatCodec::encode(const MdSubscriptionHeartbeatMsg& m,
                                                                   std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    MdSubscriptionHeartbeat msg;
    msg.wrapAndApplyHeader(buf, 0, scratch.size())
        .timestampNs(m.timestamp_ns)
        .instrumentId(m.instrument_id)
        .seqNum(m.seq_num);

    const auto total = MessageHeader::encodedLength() + MdSubscriptionHeartbeat::sbeBlockLength();
    return scratch.subspan(0, total);
}

MdSubscriptionHeartbeatMsg SbeMdSubscriptionHeartbeatCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeMdSubscriptionHeartbeatCodec::decode: too short");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != MdSubscriptionHeartbeat::sbeTemplateId())
        throw std::runtime_error("SbeMdSubscriptionHeartbeatCodec::decode: wrong template id");

    MdSubscriptionHeartbeat msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    return MdSubscriptionHeartbeatMsg{msg.timestampNs(), msg.instrumentId(), msg.seqNum()};
}

}  // namespace bpt::md_gateway::messaging
