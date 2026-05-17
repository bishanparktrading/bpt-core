#pragma once

/// @file
/// Aeron-backed concrete for api::ToxicitySubscriber.

#include "strategy/messaging/subscribers/api/toxicity_subscriber.h"

#include <Aeron.h>

#include <memory>
#include <string>

namespace bpt::strategy::messaging::aeron {

class ToxicitySubscriber final : public api::ToxicitySubscriber {
public:
    ToxicitySubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    int poll(int fragment_limit = 4) override;

private:
    std::shared_ptr<::aeron::Subscription> sub_;
};

}  // namespace bpt::strategy::messaging::aeron
