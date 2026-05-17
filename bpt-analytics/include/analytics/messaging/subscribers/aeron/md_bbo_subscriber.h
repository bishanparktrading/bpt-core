#pragma once

/// @file
/// Aeron-backed concrete for api::MdBboSubscriber. Subscribes to the
/// bpt-md-gateway BBO stream and decodes SBE `MdMarketData` before
/// dispatching to the `on_bbo` callback.

#include "analytics/messaging/subscribers/api/md_bbo_subscriber.h"

#include <bpt_common/aeron/subscriber.h>
#include <memory>
#include <string>

namespace bpt::analytics::messaging::aeron {

class MdBboSubscriber final : public api::MdBboSubscriber {
public:
    MdBboSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    int poll(int fragment_limit = 10) override;

private:
    std::unique_ptr<bpt::common::aeron::Subscriber> sub_;
};

}  // namespace bpt::analytics::messaging::aeron
