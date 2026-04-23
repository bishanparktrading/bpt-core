#pragma once

#include "refdata/messaging/messages.h"
#include "refdata/messaging/streams.h"

#include <Aeron.h>

#include <functional>
#include <memory>
#include <bpt_common/aeron/subscriber.h>

namespace bpt::refdata::messaging {

class RefdataControlSubscriber {
public:
    using RequestHandler = std::function<void(const RefdataRequest&)>;

    RefdataControlSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    // Returns number of fragments processed (0 = idle, use for idle strategy).
    int poll(RequestHandler handler);

private:
    std::unique_ptr<bpt::common::aeron::Subscriber> subscription_;
    RequestHandler current_handler_;
};

}  // namespace bpt::refdata::messaging
