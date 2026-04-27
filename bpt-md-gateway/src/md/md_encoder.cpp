#include "md_gateway/md/md_encoder.h"

namespace sbe = bpt::messages;

namespace bpt::md_gateway::md {

std::size_t MdEncoder::encode(const MdOrderBook& book, uint64_t seq_num, char* buf, std::size_t capacity) noexcept {
    auto n_bids = static_cast<uint16_t>(book.bids.size());
    auto n_asks = static_cast<uint16_t>(book.asks.size());

    if (n_bids > kMaxLevels || n_asks > kMaxLevels)
        return 0;

    std::size_t needed = sbe::MessageHeader::encodedLength() + sbe::MdOrderBook::sbeBlockLength() +
                         sbe::MdOrderBook::Bids::sbeHeaderSize() + n_bids * sbe::MdOrderBook::Bids::sbeBlockLength() +
                         sbe::MdOrderBook::Asks::sbeHeaderSize() + n_asks * sbe::MdOrderBook::Asks::sbeBlockLength();

    if (capacity < needed)
        return 0;

    sbe::MdOrderBook msg;
    msg.wrapAndApplyHeader(buf, 0, needed)
        .timestampNs(book.timestamp_ns)
        .instrumentId(book.instrument_id)
        .seqNum(seq_num);

    auto& bg = msg.bidsCount(n_bids);
    for (uint16_t i = 0; i < n_bids; ++i)
        bg.next().price(book.bids[i].first).qty(book.bids[i].second);

    auto& ag = msg.asksCount(n_asks);
    for (uint16_t i = 0; i < n_asks; ++i)
        ag.next().price(book.asks[i].first).qty(book.asks[i].second);

    return needed;
}

}  // namespace bpt::md_gateway::md
