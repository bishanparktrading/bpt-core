#pragma once

#include "muninn/messaging/messages.h"
#include "muninn/registry/instrument_registry.h"

#include <Aeron.h>

#include <memory>

namespace muninn::messaging {

class RefdataSnapshotPublisher {
public:
    RefdataSnapshotPublisher(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel, int stream_id);

    void publish(const registry::InstrumentRegistry& registry, const RefdataRequest& request, uint64_t seq_start);

private:
    std::shared_ptr<aeron::Publication> publication_;
};

}  // namespace muninn::messaging
