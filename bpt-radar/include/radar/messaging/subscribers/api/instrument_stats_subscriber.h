#pragma once

/// \file
/// Port: InstrumentStats subscriber. Aeron concrete in
/// `aeron/instrument_stats_subscriber.h`.

#include <messages/InstrumentStats.h>

#include <functional>

namespace bpt::radar::messaging::api {

class InstrumentStatsSubscriber {
public:
    using OnStatsFn = std::function<void(bpt::messages::InstrumentStats&)>;

    virtual ~InstrumentStatsSubscriber() = default;

    virtual int poll(int fragment_limit = 8) = 0;

    OnStatsFn on_stats;
};

}  // namespace bpt::radar::messaging::api
