#pragma once

/// @file
/// Aeron+SBE implementation of IStatusPublisher. Composes one codec per
/// message type (PricerHeartbeat, PricerReady) — same pattern as
/// AeronVolSurfacePublisher composes SbeVolSurfaceCodec.

#include "pricer/messaging/codecs/sbe_pricer_heartbeat_codec.h"
#include "pricer/messaging/codecs/sbe_pricer_ready_codec.h"
#include "pricer/messaging/publishers/i_status_publisher.h"

#include <Aeron.h>

#include <cstdint>
#include <memory>
#include <string>

namespace bpt::pricer::messaging {

class AeronStatusPublisher : public IStatusPublisher {
public:
    AeronStatusPublisher(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel, int32_t stream_id);

    void publish_heartbeat(uint64_t timestamp_ns, uint64_t seq_num) override;
    void publish_ready(uint64_t timestamp_ns,
                       uint8_t  exchanges_loaded,
                       uint16_t underlying_count,
                       uint32_t point_count) override;

private:
    std::shared_ptr<aeron::Publication> pub_;
    SbePricerHeartbeatCodec             hb_codec_;
    SbePricerReadyCodec                 ready_codec_;
};

}  // namespace bpt::pricer::messaging
