#include "bridge/account_subscriber.h"

#include <bifrost_protocol/AccountSnapshot.h>
#include <bifrost_protocol/MessageHeader.h>

#include <chrono>
#include <thread>
#include <yggdrasil/logging.h>

namespace bridge {

namespace {
constexpr double kE8 = 1e8;
}

AccountSubscriber::AccountSubscriber(std::shared_ptr<aeron::Aeron> aeron,
                                     const std::string& channel,
                                     int32_t stream_id) {
    const int64_t reg_id = aeron->addSubscription(channel, stream_id);
    for (int i = 0; i < 500; ++i) {
        sub_ = aeron->findSubscription(reg_id);
        if (sub_) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!sub_) {
        ygg::log::error("[bridge/Account] failed to register subscription on {} stream {}",
                        channel, stream_id);
    } else {
        ygg::log::info("[bridge/Account] subscribed on {} stream {}", channel, stream_id);
    }
}

int AccountSubscriber::poll(int fragment_limit) {
    if (!sub_) return 0;
    return sub_->poll(
        [this](const aeron::concurrent::AtomicBuffer& b,
               aeron::util::index_t o,
               aeron::util::index_t l,
               const aeron::Header& h) { on_fragment(b, o, l, h); },
        fragment_limit);
}

void AccountSubscriber::on_fragment(const aeron::concurrent::AtomicBuffer& buffer,
                                    aeron::util::index_t offset,
                                    aeron::util::index_t length,
                                    const aeron::Header& /*header*/) {
    using namespace bifrost::protocol;

    if (length < static_cast<aeron::util::index_t>(MessageHeader::encodedLength())) return;

    char* data = const_cast<char*>(reinterpret_cast<const char*>(buffer.buffer() + offset));
    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), static_cast<uint64_t>(length));

    if (hdr.templateId() != AccountSnapshot::sbeTemplateId()) return;

    AccountSnapshot msg;
    msg.wrapForDecode(data,
                      MessageHeader::encodedLength(),
                      hdr.blockLength(),
                      hdr.version(),
                      static_cast<uint64_t>(length));

    Snapshot s{};
    s.ts_ns             = msg.timestampNs();
    s.exchange_id       = static_cast<uint8_t>(msg.exchangeId());
    s.available_balance = static_cast<double>(msg.availableBalanceE8()) / kE8;
    s.total_equity      = static_cast<double>(msg.totalEquityE8()) / kE8;
    if (s.total_equity == 0.0) s.total_equity = s.available_balance;

    if (handler_) handler_(s);
}

}  // namespace bridge
