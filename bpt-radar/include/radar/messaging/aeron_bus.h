#pragma once

/// \file
/// \brief Bus boundary for bpt-radar.
///
/// Mirror of bpt-analytics: every Aeron pub/sub the service needs is built in
/// one factory so `RadarService` doesn't depend on `<Aeron.h>` directly.

#include "radar/messaging/publishers/api/market_color_publisher.h"
#include "radar/messaging/subscribers/api/funding_rate_subscriber.h"
#include "radar/messaging/subscribers/api/instrument_stats_subscriber.h"
#include "radar/messaging/subscribers/api/md_market_data_subscriber.h"
#include "radar/messaging/subscribers/api/md_trade_subscriber.h"
#include "radar/messaging/subscribers/api/refdata_perp_subscriber.h"
#include "radar/messaging/subscribers/api/vol_surface_subscriber.h"

#include <Aeron.h>

#include <memory>

namespace bpt::radar {
namespace config {
struct Settings;
}

namespace messaging {

struct RadarBus {
    std::unique_ptr<api::VolSurfaceSubscriber> surface_sub;        ///< port
    std::unique_ptr<api::InstrumentStatsSubscriber> stats_sub;     ///< port
    std::unique_ptr<api::FundingRateSubscriber> funding_sub;       ///< port
    std::unique_ptr<api::RefdataPerpSubscriber> refdata_perp_sub;  ///< port
    std::unique_ptr<api::MdTradeSubscriber> trade_sub;             ///< port
    std::unique_ptr<api::MdMarketDataSubscriber> bbo_sub;          ///< port
    std::unique_ptr<api::MarketColorPublisher> color_pub;          ///< port; aeron::MarketColorPublisher in prod
};

class RadarAeronBus {
public:
    static RadarBus build(std::shared_ptr<::aeron::Aeron> aeron, const config::Settings& settings);
};

}  // namespace messaging
}  // namespace bpt::radar
