#pragma once

/// \file
/// Port: FundingRate subscriber. Aeron concrete in
/// `aeron/funding_rate_subscriber.h`.

#include <messages/FundingRate.h>

#include <functional>

namespace bpt::radar::messaging::api {

class FundingRateSubscriber {
public:
    using OnFundingFn = std::function<void(bpt::messages::FundingRate&)>;

    virtual ~FundingRateSubscriber() = default;

    virtual int poll(int fragment_limit = 8) = 0;

    OnFundingFn on_funding;
};

}  // namespace bpt::radar::messaging::api
