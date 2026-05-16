#include "md_gateway/messaging/codecs/sbe_md_service_heartbeat_codec.h"

#include <messages/MdServiceHeartbeat.h>
#include <messages/MessageHeader.h>

#include <cstring>
#include <stdexcept>

namespace bpt::md_gateway::messaging {

using bpt::messages::MdServiceHeartbeat;
using bpt::messages::MessageHeader;

std::span<const std::byte> SbeMdServiceHeartbeatCodec::encode(const MdServiceHeartbeatMsg& m,
                                                              std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    MdServiceHeartbeat msg;
    msg.wrapAndApplyHeader(buf, 0, scratch.size()).timestampNs(m.timestamp_ns).seqNum(m.seq_num);

    const auto total = MessageHeader::encodedLength() + MdServiceHeartbeat::sbeBlockLength();
    return scratch.subspan(0, total);
}

MdServiceHeartbeatMsg SbeMdServiceHeartbeatCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeMdServiceHeartbeatCodec::decode: too short");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != MdServiceHeartbeat::sbeTemplateId())
        throw std::runtime_error("SbeMdServiceHeartbeatCodec::decode: wrong template id");

    MdServiceHeartbeat msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    return MdServiceHeartbeatMsg{msg.timestampNs(), msg.seqNum()};
}

}  // namespace bpt::md_gateway::messaging
