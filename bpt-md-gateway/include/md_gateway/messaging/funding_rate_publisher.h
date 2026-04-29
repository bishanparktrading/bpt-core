#pragma once

#include "md_gateway/messaging/i_funding_rate_publisher.h"

#include <Aeron.h>

#include <memory>
#include <string>
#include <bpt_common/aeron/publisher.h>

namespace bpt::md_gateway::messaging {

// Publishes FundingRate SBE messages (template id=18) on stream 1005.
// Same wire format as Refdata previously published — Strategy consumer unchanged.
class FundingRatePublisher final : public IFundingRatePublisher {
public:
    FundingRatePublisher(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    void publish(const FundingRateUpdate& fr) override;

private:
    bpt::common::aeron::Publisher publisher_;
};

}  // namespace bpt::md_gateway::messaging
