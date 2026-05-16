#include "pricer/messaging/codecs/sbe_vol_surface_codec.h"

#include <messages/MessageHeader.h>
#include <messages/VolSurface.h>

#include <cstring>
#include <stdexcept>

namespace bpt::pricer::messaging {

using bpt::messages::MessageHeader;
using bpt::messages::VolSurface;

std::span<const std::byte> SbeVolSurfaceCodec::encode(const surface::VolSurfaceGrid& grid,
                                                      uint64_t timestamp_ns,
                                                      std::span<std::byte> scratch) {
    auto* buf = reinterpret_cast<char*>(scratch.data());
    std::memset(buf, 0, scratch.size());

    MessageHeader hdr;
    VolSurface msg;

    hdr.wrap(buf, 0, VolSurface::sbeSchemaVersion(), scratch.size())
        .blockLength(VolSurface::sbeBlockLength())
        .templateId(VolSurface::sbeTemplateId())
        .schemaId(VolSurface::sbeSchemaId())
        .version(VolSurface::sbeSchemaVersion());

    msg.wrapForEncode(buf, hdr.encodedLength(), scratch.size());
    msg.timestampNs(timestamp_ns);
    msg.exchangeId(grid.exchange_id);
    msg.putUnderlying(grid.underlying);
    msg.seqNum(grid.seq_num);

    auto& pts = msg.pointsCount(static_cast<uint16_t>(grid.points.size()));
    for (const auto& p : grid.points) {
        pts.next()
            .instrumentId(p.instrument_id)
            .expiryDate(p.expiry_date)
            .strikePrice(p.strike_price)
            .optionSide(p.option_side)
            .impliedVol(p.implied_vol)
            .forwardPrice(p.forward_price)
            .timeToExpiry(p.time_to_expiry)
            .bidIv(p.bid_iv)
            .askIv(p.ask_iv)
            .bidPrice(p.bid_price)
            .askPrice(p.ask_price)
            .delta(p.delta)
            .gamma(p.gamma)
            .vega(p.vega)
            .theta(p.theta);
    }

    const auto total = MessageHeader::encodedLength() + msg.encodedLength();
    return scratch.subspan(0, total);
}

surface::VolSurfaceGrid SbeVolSurfaceCodec::decode(std::span<const std::byte> bytes) {
    if (bytes.size() < MessageHeader::encodedLength())
        throw std::runtime_error("SbeVolSurfaceCodec::decode: bytes too short for MessageHeader");

    auto* data = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
    const uint64_t len = bytes.size();

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), len);

    if (hdr.templateId() != VolSurface::sbeTemplateId())
        throw std::runtime_error("SbeVolSurfaceCodec::decode: wrong template id");

    VolSurface msg;
    msg.wrapForDecode(data, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), len);

    surface::VolSurfaceGrid grid;
    grid.exchange_id = msg.exchangeId();
    grid.underlying = msg.getUnderlyingAsString();
    grid.seq_num = msg.seqNum();

    auto& pts = msg.points();
    grid.points.reserve(pts.count());
    while (pts.hasNext()) {
        pts.next();
        surface::IvPoint p;
        p.instrument_id = pts.instrumentId();
        p.expiry_date = pts.expiryDate();
        p.strike_price = pts.strikePrice();
        p.option_side = pts.optionSide();
        p.implied_vol = pts.impliedVol();
        p.forward_price = pts.forwardPrice();
        p.time_to_expiry = pts.timeToExpiry();
        p.bid_iv = pts.bidIv();
        p.ask_iv = pts.askIv();
        p.bid_price = pts.bidPrice();
        p.ask_price = pts.askPrice();
        p.delta = pts.delta();
        p.gamma = pts.gamma();
        p.vega = pts.vega();
        p.theta = pts.theta();
        grid.points.push_back(p);
    }

    return grid;
}

}  // namespace bpt::pricer::messaging
