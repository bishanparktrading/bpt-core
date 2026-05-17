#pragma once

/// \file
/// Aeron concrete: subscribes to bpt-md-gateway's InstrumentStats stream
/// (typically 2004). Slow-cadence per-instrument snapshot — OI, mark /
/// index / last price, 24h volume.

#include "radar/messaging/subscribers/api/instrument_stats_subscriber.h"

#include <Aeron.h>

#include <memory>
#include <string>

namespace bpt::radar::messaging::aeron {

class InstrumentStatsSubscriber final : public api::InstrumentStatsSubscriber {
public:
    InstrumentStatsSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    int poll(int fragment_limit = 8) override;

private:
    void handle_fragment(::aeron::AtomicBuffer& buffer,
                         ::aeron::util::index_t offset,
                         ::aeron::util::index_t length,
                         ::aeron::Header& header);

    std::shared_ptr<::aeron::Subscription> sub_;
};

}  // namespace bpt::radar::messaging::aeron
