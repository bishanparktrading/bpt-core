#include "surtr/messaging/status_publisher.h"

#include <bifrost_protocol/MessageHeader.h>
#include <bifrost_protocol/SurtrHeartbeat.h>
#include <bifrost_protocol/SurtrReady.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>
#include <thread>

namespace surtr::messaging {

StatusPublisher::StatusPublisher(std::shared_ptr<aeron::Aeron> aeron,
                                 const std::string& channel,
                                 int32_t stream_id,
                                 int pub_timeout_ms,
                                 int pub_poll_interval_ms) {
    const int64_t reg_id = aeron->addPublication(channel, stream_id);
    const int max_retries = pub_timeout_ms / std::max(pub_poll_interval_ms, 1);
    const auto poll_interval = std::chrono::milliseconds(pub_poll_interval_ms);

    for (int i = 0; i < max_retries; ++i) {
        pub_ = aeron->findPublication(reg_id);
        if (pub_)
            break;
        std::this_thread::sleep_for(poll_interval);
    }

    if (!pub_) {
        spdlog::error("[StatusPublisher] Failed to find publication on {} stream {}", channel, stream_id);
    } else {
        spdlog::info("[StatusPublisher] Publication ready on {} stream {}", channel, stream_id);
    }
}

void StatusPublisher::publish_heartbeat(uint64_t timestamp_ns, uint64_t seq_num) {
    if (!pub_)
        return;

    alignas(8) char buf[128];
    std::memset(buf, 0, sizeof(buf));

    using namespace bifrost::protocol;

    MessageHeader hdr;
    SurtrHeartbeat msg;

    hdr.wrap(buf, 0, SurtrHeartbeat::sbeSchemaVersion(), sizeof(buf))
        .blockLength(SurtrHeartbeat::sbeBlockLength())
        .templateId(SurtrHeartbeat::sbeTemplateId())
        .schemaId(SurtrHeartbeat::sbeSchemaId())
        .version(SurtrHeartbeat::sbeSchemaVersion());

    msg.wrapForEncode(buf, hdr.encodedLength(), sizeof(buf));
    msg.timestampNs(timestamp_ns);
    msg.seqNum(seq_num);

    const auto total = MessageHeader::encodedLength() + msg.encodedLength();
    aeron::concurrent::AtomicBuffer buffer(reinterpret_cast<uint8_t*>(buf), total);
    pub_->offer(buffer, 0, static_cast<int32_t>(total));
}

void StatusPublisher::publish_ready(uint64_t timestamp_ns,
                                    uint8_t exchanges_loaded,
                                    uint16_t underlying_count,
                                    uint32_t point_count) {
    if (!pub_)
        return;

    alignas(8) char buf[128];
    std::memset(buf, 0, sizeof(buf));

    using namespace bifrost::protocol;

    MessageHeader hdr;
    SurtrReady msg;

    hdr.wrap(buf, 0, SurtrReady::sbeSchemaVersion(), sizeof(buf))
        .blockLength(SurtrReady::sbeBlockLength())
        .templateId(SurtrReady::sbeTemplateId())
        .schemaId(SurtrReady::sbeSchemaId())
        .version(SurtrReady::sbeSchemaVersion());

    msg.wrapForEncode(buf, hdr.encodedLength(), sizeof(buf));
    msg.timestampNs(timestamp_ns);
    msg.exchangesLoaded(exchanges_loaded);
    msg.underlyingCount(underlying_count);
    msg.pointCount(point_count);

    const auto total = MessageHeader::encodedLength() + msg.encodedLength();
    aeron::concurrent::AtomicBuffer buffer(reinterpret_cast<uint8_t*>(buf), total);
    pub_->offer(buffer, 0, static_cast<int32_t>(total));

    spdlog::info("[Surtr] Published SurtrReady: exchanges=0x{:02x} underlyings={} points={}",
                 exchanges_loaded,
                 underlying_count,
                 point_count);
}

}  // namespace surtr::messaging
