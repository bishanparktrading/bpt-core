#include "md_gateway/messaging/codecs/sbe_instrument_stats_codec.h"

#include <messages/InstrumentStats.h>
#include <messages/MessageHeader.h>

#include <cstring>
#include <stdexcept>

namespace bpt::md_gateway::messaging {

using bpt::messages::InstrumentStats;
using bpt::messages::MessageHeader;

std::span<const std::byte> SbeInstrumentStatsCodec::encode(const InstrumentStatsUpdate& stats,
                                                           std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    InstrumentStats msg;
    msg.wrapAndApplyHeader(buf, 0, scratch.size())
        .exchangeId(stats.exchange_id)
        .instrumentId(stats.instrument_id)
        .openInterest(stats.open_interest)
        .volume24h(stats.volume_24h)
        .markPrice(stats.mark_price)
        .indexPrice(stats.index_price)
        .lastPrice(stats.last_price)
        .collectedTs(stats.collected_ts_ns);

    const auto total = MessageHeader::encodedLength() + InstrumentStats::sbeBlockLength();
    return scratch.subspan(0, total);
}

InstrumentStatsUpdate SbeInstrumentStatsCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeInstrumentStatsCodec::decode: too short");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != InstrumentStats::sbeTemplateId())
        throw std::runtime_error("SbeInstrumentStatsCodec::decode: wrong template id");

    InstrumentStats msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    InstrumentStatsUpdate out;
    out.exchange_id = msg.exchangeId();
    out.instrument_id = msg.instrumentId();
    out.open_interest = msg.openInterest();
    out.volume_24h = msg.volume24h();
    out.mark_price = msg.markPrice();
    out.index_price = msg.indexPrice();
    out.last_price = msg.lastPrice();
    out.collected_ts_ns = msg.collectedTs();
    return out;
}

}  // namespace bpt::md_gateway::messaging
