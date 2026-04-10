#pragma once

#include "muninn/refdata/funding_rate.h"

#include <Aeron.h>

#include <memory>

namespace muninn::messaging {

// Publishes FeeSchedule SBE messages (template id=19) on stream 1004.
class FeeSchedulePublisher {
public:
    FeeSchedulePublisher(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel, int stream_id);

    void publish(const refdata::FeeScheduleState& fs);

private:
    std::shared_ptr<aeron::Publication> publication_;
};

}  // namespace muninn::messaging
