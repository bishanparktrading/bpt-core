#include "heimdall/messaging/heartbeat_publisher.h"

#include <bifrost_protocol/MessageHeader.h>
#include <bifrost_protocol/OrderGatewayHeartbeat.h>

#include <chrono>
#include <thread>
#include <yggdrasil/aeron/aeron_utils.h>

namespace heimdall::messaging {

HeartbeatPublisher::HeartbeatPublisher(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel, int stream_id) {
    publication_ = ygg::aeron::wait_for_publication(aeron, channel, stream_id);
}

void HeartbeatPublisher::publish(uint8_t service_id, uint16_t orders_in_flight, uint8_t exchange_status) {
    using namespace bifrost::protocol;

    constexpr std::size_t kBufSize = MessageHeader::encodedLength() + OrderGatewayHeartbeat::sbeBlockLength();
    char buf[kBufSize]{};

    OrderGatewayHeartbeat msg;
    msg.wrapAndApplyHeader(buf, 0, kBufSize)
        .serviceId(service_id)
        .timestampNs(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count()))
        .ordersInFlight(orders_in_flight)
        .exchangeStatus(exchange_status);

    aeron::AtomicBuffer ab(reinterpret_cast<uint8_t*>(buf), static_cast<aeron::util::index_t>(kBufSize));

    while (publication_->offer(ab, 0, static_cast<aeron::util::index_t>(kBufSize)) < 0) {
        std::this_thread::yield();
    }
}

}  // namespace heimdall::messaging
