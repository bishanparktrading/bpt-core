#pragma once

/// \file
/// Aeron concrete: subscribes to bpt-md-gateway's FundingRate stream
/// (typically 1005). SBE-decoded; dispatches each fragment via the
/// inherited `on_funding` callback.

#include "radar/messaging/subscribers/api/funding_rate_subscriber.h"

#include <Aeron.h>

#include <memory>
#include <string>

namespace bpt::radar::messaging::aeron {

class FundingRateSubscriber final : public api::FundingRateSubscriber {
public:
    FundingRateSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    int poll(int fragment_limit = 8) override;

private:
    void handle_fragment(::aeron::AtomicBuffer& buffer,
                         ::aeron::util::index_t offset,
                         ::aeron::util::index_t length,
                         ::aeron::Header& header);

    std::shared_ptr<::aeron::Subscription> sub_;
};

}  // namespace bpt::radar::messaging::aeron
