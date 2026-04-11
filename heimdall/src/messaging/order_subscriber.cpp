#include "heimdall/messaging/order_subscriber.h"

#include <bifrost_protocol/AccountSnapshotRequest.h>
#include <bifrost_protocol/CancelAll.h>
#include <bifrost_protocol/CancelOrder.h>
#include <bifrost_protocol/MessageHeader.h>
#include <bifrost_protocol/ModifyOrder.h>
#include <bifrost_protocol/NewOrder.h>

#include <yggdrasil/aeron/aeron_utils.h>
#include <yggdrasil/logging.h>

namespace heimdall::messaging {

OrderSubscriber::OrderSubscriber(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel, int stream_id) {
    subscription_ = ygg::aeron::wait_for_subscription(aeron, channel, stream_id);

    assembler_ = std::make_unique<aeron::FragmentAssembler>(
        [this](aeron::AtomicBuffer& buf, aeron::util::index_t offset, aeron::util::index_t length, aeron::Header& hdr) {
            handle_fragment(buf, offset, length, hdr);
        });
}

void OrderSubscriber::handle_fragment(aeron::AtomicBuffer& buf,
                                      aeron::util::index_t offset,
                                      aeron::util::index_t length,
                                      aeron::Header& /*hdr*/) {
    using namespace bifrost::protocol;

    if (static_cast<std::size_t>(length) < MessageHeader::encodedLength())
        return;

    char* data = reinterpret_cast<char*>(buf.buffer()) + offset;
    MessageHeader hdr(data, static_cast<std::size_t>(length));

    const uint16_t tmpl = hdr.templateId();

    if (tmpl == NewOrder::sbeTemplateId()) {
        NewOrder msg;
        msg.wrapForDecode(data,
                          MessageHeader::encodedLength(),
                          hdr.blockLength(),
                          hdr.version(),
                          static_cast<std::size_t>(length));
        if (on_new_order)
            on_new_order(msg);

    } else if (tmpl == CancelOrder::sbeTemplateId()) {
        CancelOrder msg;
        msg.wrapForDecode(data,
                          MessageHeader::encodedLength(),
                          hdr.blockLength(),
                          hdr.version(),
                          static_cast<std::size_t>(length));
        if (on_cancel)
            on_cancel(msg);

    } else if (tmpl == CancelAll::sbeTemplateId()) {
        CancelAll msg;
        msg.wrapForDecode(data,
                          MessageHeader::encodedLength(),
                          hdr.blockLength(),
                          hdr.version(),
                          static_cast<std::size_t>(length));
        if (on_cancel_all)
            on_cancel_all(msg);

    } else if (tmpl == ModifyOrder::sbeTemplateId()) {
        ModifyOrder msg;
        msg.wrapForDecode(data,
                          MessageHeader::encodedLength(),
                          hdr.blockLength(),
                          hdr.version(),
                          static_cast<std::size_t>(length));
        if (on_modify)
            on_modify(msg);

    } else if (tmpl == AccountSnapshotRequest::sbeTemplateId()) {
        AccountSnapshotRequest msg;
        msg.wrapForDecode(data,
                          MessageHeader::encodedLength(),
                          hdr.blockLength(),
                          hdr.version(),
                          static_cast<std::size_t>(length));
        if (on_account_snapshot_request)
            on_account_snapshot_request(msg);

    } else {
        ygg::log::warn("[Heimdall] OrderSubscriber: unknown templateId={}", tmpl);
    }
}

int OrderSubscriber::poll(int fragment_limit) {
    return subscription_->poll(assembler_->handler(), fragment_limit);
}

}  // namespace heimdall::messaging
