#pragma once

/// \file
/// \brief Binance MD frame decoder (JSON → SBE).

#include "md_gateway/adapter/common/i_exchange_decoder.h"
#include "md_gateway/adapter/common/subscription_map.h"

#include <bpt_common/util/latency_histogram.h>

namespace bpt::md_gateway::adapter {

/// \brief Decodes Binance combined-stream WS frames and publishes SBE.
///
/// Handled message types:
///   - `<sym>@bookTicker` → publish_bbo
///   - `<sym>@aggTrade`   → publish_trade
///
/// Funding rates arrive on a separate BinanceMdAdapter thread
/// (fstream.binance.com) and are not handled here.
class BinanceMdDecoder : public IExchangeDecoder {
public:
    explicit BinanceMdDecoder(const SubscriptionMap& subs) : subs_(subs) {}

    void decode(std::string_view payload,
               uint64_t recv_ns,
               messaging::IMdPublisher& pub,
               messaging::FundingRateCallback& on_funding_rate) override;

    bpt::common::util::LatencyHistogram decode_lat_;

private:
    const SubscriptionMap& subs_;
    uint64_t tick_count_{0};
};

}  // namespace bpt::md_gateway::adapter
