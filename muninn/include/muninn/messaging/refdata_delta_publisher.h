#pragma once

#include "muninn/refdata/instrument.h"

#include <Aeron.h>

#include <bifrost_protocol/DeltaUpdateType.h>

#include <memory>

namespace muninn::messaging {

class RefdataDeltaPublisher {
public:
    RefdataDeltaPublisher(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel, int stream_id);

    void publish_delta(bifrost::protocol::DeltaUpdateType::Value update_type, const refdata::Instrument& inst);

    // Publish a heartbeat on the delta stream so subscribers can detect silent failures.
    // Uses DeltaUpdateType::NULL_VALUE with instrumentId=0; sequence number increments
    // so gap detection works uniformly across real deltas and heartbeats.
    void publish_heartbeat();

    uint64_t current_sequence() const { return seq_; }

private:
    std::shared_ptr<aeron::Publication> publication_;
    uint64_t seq_ = 0;
};

}  // namespace muninn::messaging
