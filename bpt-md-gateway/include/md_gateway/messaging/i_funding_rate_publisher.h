#pragma once

/// \file
/// \brief Outbound port: funding rate publish.

#include <messages/ExchangeId.h>

#include <cstdint>
#include <functional>

namespace bpt::md_gateway::messaging {

struct FundingRateUpdate {
    uint64_t instrument_id;
    bpt::messages::ExchangeId::Value exchange_id;
    int32_t rate_bps;             // signed; rate * 1e6 (e.g. 0.0001 rate → 100 bps)
    uint64_t next_funding_ts_ns;  // 0 if not provided by exchange
    uint64_t collected_ts_ns;
};

using FundingRateCallback = std::function<void(const FundingRateUpdate&)>;

class IFundingRatePublisher {
public:
    virtual ~IFundingRatePublisher() = default;

    virtual void publish(const FundingRateUpdate& fr) = 0;
};

}  // namespace bpt::md_gateway::messaging
