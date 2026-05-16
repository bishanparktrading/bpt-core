#include "pricer/messaging/codecs/sbe_pricer_ready_codec.h"

#include <messages/MessageHeader.h>
#include <messages/PricerReady.h>

#include <cstring>
#include <stdexcept>

namespace bpt::pricer::messaging {

using bpt::messages::MessageHeader;
using bpt::messages::PricerReady;

std::span<const std::byte> SbePricerReadyCodec::encode(const PricerReadyMsg& m, std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    MessageHeader hdr;
    PricerReady msg;
    hdr.wrap(buf, 0, PricerReady::sbeSchemaVersion(), scratch.size())
        .blockLength(PricerReady::sbeBlockLength())
        .templateId(PricerReady::sbeTemplateId())
        .schemaId(PricerReady::sbeSchemaId())
        .version(PricerReady::sbeSchemaVersion());

    msg.wrapForEncode(buf, hdr.encodedLength(), scratch.size());
    msg.timestampNs(m.timestamp_ns)
        .exchangesLoaded(m.exchanges_loaded)
        .underlyingCount(m.underlying_count)
        .pointCount(m.point_count);

    const auto total = MessageHeader::encodedLength() + msg.encodedLength();
    return scratch.subspan(0, total);
}

PricerReadyMsg SbePricerReadyCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbePricerReadyCodec::decode: bytes too short for MessageHeader");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != PricerReady::sbeTemplateId())
        throw std::runtime_error("SbePricerReadyCodec::decode: wrong template id");

    PricerReady msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    return PricerReadyMsg{
        msg.timestampNs(),
        msg.exchangesLoaded(),
        msg.underlyingCount(),
        msg.pointCount(),
    };
}

}  // namespace bpt::pricer::messaging
