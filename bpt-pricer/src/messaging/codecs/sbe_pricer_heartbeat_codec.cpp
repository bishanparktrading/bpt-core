#include "pricer/messaging/codecs/sbe_pricer_heartbeat_codec.h"

#include <messages/MessageHeader.h>
#include <messages/PricerHeartbeat.h>

#include <cstring>
#include <stdexcept>

namespace bpt::pricer::messaging {

using bpt::messages::MessageHeader;
using bpt::messages::PricerHeartbeat;

std::span<const std::byte> SbePricerHeartbeatCodec::encode(const PricerHeartbeatMsg& m, std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    MessageHeader hdr;
    PricerHeartbeat msg;
    hdr.wrap(buf, 0, PricerHeartbeat::sbeSchemaVersion(), scratch.size())
        .blockLength(PricerHeartbeat::sbeBlockLength())
        .templateId(PricerHeartbeat::sbeTemplateId())
        .schemaId(PricerHeartbeat::sbeSchemaId())
        .version(PricerHeartbeat::sbeSchemaVersion());

    msg.wrapForEncode(buf, hdr.encodedLength(), scratch.size());
    msg.timestampNs(m.timestamp_ns).seqNum(m.seq_num);

    const auto total = MessageHeader::encodedLength() + msg.encodedLength();
    return scratch.subspan(0, total);
}

PricerHeartbeatMsg SbePricerHeartbeatCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbePricerHeartbeatCodec::decode: bytes too short for MessageHeader");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != PricerHeartbeat::sbeTemplateId())
        throw std::runtime_error("SbePricerHeartbeatCodec::decode: wrong template id");

    PricerHeartbeat msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    return PricerHeartbeatMsg{msg.timestampNs(), msg.seqNum()};
}

}  // namespace bpt::pricer::messaging
