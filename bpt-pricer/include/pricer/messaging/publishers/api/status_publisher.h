#pragma once

/// @file
/// Port for the pricer's status stream (PricerHeartbeat + PricerReady).
/// Pricer service uses these to signal liveness and "surface populated"
/// to downstream consumers (bridge, strategy, radar). Aeron+SBE concrete
/// in `aeron/status_publisher.h`; a sim variant would dispatch the
/// domain structs directly to whatever consumer is interested.

#include <cstdint>

namespace bpt::pricer::messaging::api {

class StatusPublisher {
public:
    virtual ~StatusPublisher() = default;

    virtual void publish_heartbeat(uint64_t timestamp_ns, uint64_t seq_num) = 0;
    virtual void publish_ready(uint64_t timestamp_ns,
                               uint8_t exchanges_loaded,
                               uint16_t underlying_count,
                               uint32_t point_count) = 0;
};

}  // namespace bpt::pricer::messaging::api
