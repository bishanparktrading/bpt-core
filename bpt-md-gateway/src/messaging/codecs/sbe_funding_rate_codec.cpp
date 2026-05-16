#include "md_gateway/messaging/codecs/sbe_funding_rate_codec.h"

#include <messages/FundingRate.h>
#include <messages/MessageHeader.h>

#include <cstring>
#include <stdexcept>

namespace bpt::md_gateway::messaging {

using bpt::messages::FundingRate;
using bpt::messages::MessageHeader;

std::span<const std::byte> SbeFundingRateCodec::encode(const FundingRateUpdate& fr, std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    FundingRate msg;
    msg.wrapAndApplyHeader(buf, 0, scratch.size())
        .exchangeId(fr.exchange_id)
        .instrumentId(fr.instrument_id)
        .rateBps(fr.rate_bps)
        .nextFundingTs(fr.next_funding_ts_ns)
        .collectedTs(fr.collected_ts_ns);

    const auto total = MessageHeader::encodedLength() + FundingRate::sbeBlockLength();
    return scratch.subspan(0, total);
}

FundingRateUpdate SbeFundingRateCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeFundingRateCodec::decode: too short");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != FundingRate::sbeTemplateId())
        throw std::runtime_error("SbeFundingRateCodec::decode: wrong template id");

    FundingRate msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    FundingRateUpdate out;
    out.exchange_id = msg.exchangeId();
    out.instrument_id = msg.instrumentId();
    out.rate_bps = msg.rateBps();
    out.next_funding_ts_ns = msg.nextFundingTs();
    out.collected_ts_ns = msg.collectedTs();
    return out;
}

}  // namespace bpt::md_gateway::messaging
