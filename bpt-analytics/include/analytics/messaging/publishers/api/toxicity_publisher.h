#pragma once

/// @file
/// Port for the analytics toxicity stream. AnalyticsService publishes
/// a `ToxicityUpdate` per scoring interval; aeron concrete is the
/// Aeron+POD implementation, a sim variant would dispatch the struct
/// directly to whatever consumer (a strategy running in the same
/// process, the dashboard side under test).

#include "analytics/messaging/toxicity_update.h"

namespace bpt::analytics::messaging::api {

class ToxicityPublisher {
public:
    virtual ~ToxicityPublisher() = default;

    /// Returns true if the underlying transport accepted the message.
    /// False = drop (idempotent stream — next tick supersedes).
    virtual bool publish(const ToxicityUpdate& update) = 0;
};

}  // namespace bpt::analytics::messaging::api
