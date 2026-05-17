#pragma once

/// @file
/// Port: bpt-analytics toxicity stream subscriber. Pulls
/// `ToxicityUpdate` POD messages and dispatches them via the
/// `on_update` std::function callback. Aeron concrete in
/// `aeron/toxicity_subscriber.h`.

#include <analytics/messaging/toxicity_update.h>

#include <functional>

namespace bpt::strategy::messaging::api {

class ToxicitySubscriber {
public:
    using OnUpdateFn = std::function<void(const bpt::analytics::messaging::ToxicityUpdate&)>;

    virtual ~ToxicitySubscriber() = default;

    virtual int poll(int fragment_limit = 4) = 0;

    OnUpdateFn on_update;
};

}  // namespace bpt::strategy::messaging::api
