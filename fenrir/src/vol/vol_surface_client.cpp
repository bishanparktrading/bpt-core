#include "fenrir/vol/vol_surface_client.h"

#include <bifrost_protocol/MessageHeader.h>
#include <bifrost_protocol/SurtrHeartbeat.h>
#include <bifrost_protocol/SurtrReady.h>
#include <bifrost_protocol/VolSurface.h>

#include <chrono>
#include <stdexcept>
#include <thread>
#include <yggdrasil/logging.h>

namespace fenrir::vol {

VolSurfaceClient::VolSurfaceClient(std::shared_ptr<aeron::Aeron> aeron,
                                   const std::string& channel,
                                   int vol_surface_stream,
                                   int surtr_status_stream,
                                   int pub_timeout_ms,
                                   int pub_poll_interval_ms) {
    long surface_id = aeron->addSubscription(channel, vol_surface_stream);
    long status_id = aeron->addSubscription(channel, surtr_status_stream);

    const int max_retries = pub_timeout_ms / std::max(pub_poll_interval_ms, 1);
    const auto poll_interval = std::chrono::milliseconds(pub_poll_interval_ms);

    for (int i = 0; i < max_retries; ++i) {
        if (!surface_sub_)
            surface_sub_ = aeron->findSubscription(surface_id);
        if (!status_sub_)
            status_sub_ = aeron->findSubscription(status_id);
        if (surface_sub_ && status_sub_)
            break;
        std::this_thread::sleep_for(poll_interval);
    }

    if (!surface_sub_)
        throw std::runtime_error("Timed out waiting for VolSurface subscription");
    if (!status_sub_)
        throw std::runtime_error("Timed out waiting for Surtr status subscription");

    // FragmentAssembler for surface stream — VolSurface messages can be large
    surface_assembler_ = std::make_unique<aeron::FragmentAssembler>(
        [this](aeron::AtomicBuffer& buffer,
               aeron::util::index_t offset,
               aeron::util::index_t length,
               aeron::Header& header) { handle_surface_fragment(buffer, offset, length, header); });

    ygg::log::info("[VolSurfaceClient] Subscriptions ready: surface={} status={}",
                   vol_surface_stream,
                   surtr_status_stream);
}

int VolSurfaceClient::poll(int fragment_limit) {
    int frags = 0;
    if (surface_sub_)
        frags += surface_sub_->poll(surface_assembler_->handler(), fragment_limit);
    if (status_sub_) {
        frags +=
            status_sub_->poll([this](aeron::AtomicBuffer& buffer,
                                     aeron::util::index_t offset,
                                     aeron::util::index_t length,
                                     aeron::Header& header) { handle_status_fragment(buffer, offset, length, header); },
                              fragment_limit);
    }
    return frags;
}

void VolSurfaceClient::handle_surface_fragment(aeron::AtomicBuffer& buffer,
                                               aeron::util::index_t offset,
                                               aeron::util::index_t length,
                                               aeron::Header& /*header*/) {
    using namespace bifrost::protocol;

    if (length < static_cast<aeron::util::index_t>(MessageHeader::encodedLength()))
        return;

    char* data = reinterpret_cast<char*>(buffer.buffer()) + offset;

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), static_cast<uint64_t>(length));

    if (hdr.templateId() != VolSurface::sbeTemplateId())
        return;

    VolSurface msg;
    msg.wrapForDecode(data,
                      MessageHeader::encodedLength(),
                      hdr.blockLength(),
                      hdr.version(),
                      static_cast<uint64_t>(length));

    if (on_vol_surface)
        on_vol_surface(msg);
}

void VolSurfaceClient::handle_status_fragment(aeron::AtomicBuffer& buffer,
                                              aeron::util::index_t offset,
                                              aeron::util::index_t length,
                                              aeron::Header& /*header*/) {
    using namespace bifrost::protocol;

    if (length < static_cast<aeron::util::index_t>(MessageHeader::encodedLength()))
        return;

    char* data = reinterpret_cast<char*>(buffer.buffer()) + offset;

    MessageHeader hdr;
    hdr.wrap(data, 0, MessageHeader::sbeSchemaVersion(), static_cast<uint64_t>(length));

    const auto tid = hdr.templateId();

    if (tid == SurtrHeartbeat::sbeTemplateId()) {
        SurtrHeartbeat hb;
        hb.wrapForDecode(data,
                         MessageHeader::encodedLength(),
                         hdr.blockLength(),
                         hdr.version(),
                         static_cast<uint64_t>(length));
        last_heartbeat_ns_ = hb.timestampNs();

    } else if (tid == SurtrReady::sbeTemplateId()) {
        SurtrReady ready;
        ready.wrapForDecode(data,
                            MessageHeader::encodedLength(),
                            hdr.blockLength(),
                            hdr.version(),
                            static_cast<uint64_t>(length));

        ygg::log::info("[VolSurfaceClient] SurtrReady: exchanges=0x{:02x} underlyings={} points={}",
                       ready.exchangesLoaded(),
                       ready.underlyingCount(),
                       ready.pointCount());

        if (on_ready)
            on_ready(ready.exchangesLoaded(), ready.underlyingCount(), ready.pointCount());
    }
}

}  // namespace fenrir::vol
