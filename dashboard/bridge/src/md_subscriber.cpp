#include "bridge/md_subscriber.h"

#include <messages/MdMarketData.h>
#include <messages/MessageHeader.h>
#include <bpt_common/logging.h>

namespace bridge {

MdSubscriber::MdSubscriber(std::shared_ptr<::aeron::Aeron> aeron,
                           const std::string& channel,
                           int32_t stream_id) {
    sub_ = std::make_unique<bpt::common::aeron::Subscriber>(
        std::move(aeron), channel, stream_id,
        [this](::aeron::AtomicBuffer& b, ::aeron::util::index_t o,
               ::aeron::util::index_t l, ::aeron::Header& h) {
            on_fragment(b, o, l, h);
        });
    bpt::common::log::info("[bridge/MD] subscribed on {} stream {}", channel, stream_id);
}

int MdSubscriber::poll(int fragment_limit) {
    return sub_ ? sub_->poll(fragment_limit) : 0;
}

void MdSubscriber::on_fragment(::aeron::AtomicBuffer& buffer,
                               ::aeron::util::index_t offset,
                               ::aeron::util::index_t length,
                               ::aeron::Header& /*header*/) {
    using namespace bpt::messages;

    if (length < static_cast<::aeron::util::index_t>(MessageHeader::encodedLength())) return;

    auto* data = reinterpret_cast<char*>(buffer.buffer() + offset);
    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), static_cast<uint64_t>(length));

    if (hdr.templateId() != MdMarketData::sbeTemplateId()) return;

    MdMarketData md;
    md.wrapForDecode(data, MessageHeader::encodedLength(),
                     hdr.blockLength(), hdr.version(),
                     static_cast<uint64_t>(length));

    const double bid = md.bidPrice();
    const double ask = md.askPrice();
    if (bid <= 0 || ask <= 0) return;
    const double mid = (bid + ask) * 0.5;

    if (handler_) handler_(md.instrumentId(), mid, md.timestampNs());
}

}  // namespace bridge
