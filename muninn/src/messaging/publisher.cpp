#include "muninn/messaging/publisher.h"

#include <iostream>
#include <spdlog/spdlog.h>

namespace muninn::messaging {

RefdataPublisher::RefdataPublisher() {
    // Initialize Aeron publications here
}

RefdataPublisher::~RefdataPublisher() = default;

void RefdataPublisher::publishSnapshot(const std::vector<refdata::Instrument>& instruments) {
    spdlog::info("Publishing snapshot of {} instruments", instruments.size());
    // Iterate and send via Aeron
}

void RefdataPublisher::publishDelta(const refdata::Instrument& instrument) {
    spdlog::info("Publishing delta for instrument: {}", instrument.inst_uid);
    // Send via Aeron
}

}  // namespace muninn::messaging
