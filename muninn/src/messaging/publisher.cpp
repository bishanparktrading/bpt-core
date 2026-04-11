#include "muninn/messaging/publisher.h"

#include <iostream>

namespace muninn::messaging {

RefdataPublisher::RefdataPublisher() {
    // Initialize Aeron publications here
}

RefdataPublisher::~RefdataPublisher() = default;

void RefdataPublisher::publishSnapshot(const std::vector<refdata::Instrument>& instruments) {
    ygg::log::info("Publishing snapshot of {} instruments", instruments.size());
    // Iterate and send via Aeron
}

void RefdataPublisher::publishDelta(const refdata::Instrument& instrument) {
    ygg::log::info("Publishing delta for instrument: {}", instrument.inst_uid);
    // Send via Aeron
}

}  // namespace muninn::messaging
