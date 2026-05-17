#include "radar/messaging/aeron_bus.h"

#include "radar/config/settings.h"
#include "radar/messaging/publishers/aeron/market_color_publisher.h"
#include "radar/messaging/subscribers/aeron/funding_rate_subscriber.h"
#include "radar/messaging/subscribers/aeron/instrument_stats_subscriber.h"
#include "radar/messaging/subscribers/aeron/md_market_data_subscriber.h"
#include "radar/messaging/subscribers/aeron/md_trade_subscriber.h"
#include "radar/messaging/subscribers/aeron/refdata_perp_subscriber.h"
#include "radar/messaging/subscribers/aeron/vol_surface_subscriber.h"

namespace bpt::radar::messaging {

RadarBus RadarAeronBus::build(std::shared_ptr<::aeron::Aeron> aeron, const config::Settings& settings) {
    RadarBus bus;
    bus.surface_sub = std::make_unique<aeron::VolSurfaceSubscriber>(aeron,
                                                                    settings.vol_surface.channel,
                                                                    settings.vol_surface.stream_id);
    bus.stats_sub = std::make_unique<aeron::InstrumentStatsSubscriber>(aeron,
                                                                       settings.instrument_stats.channel,
                                                                       settings.instrument_stats.stream_id);
    bus.funding_sub = std::make_unique<aeron::FundingRateSubscriber>(aeron,
                                                                     settings.funding_rate.channel,
                                                                     settings.funding_rate.stream_id);
    bus.refdata_perp_sub = std::make_unique<aeron::RefdataPerpSubscriber>(aeron,
                                                                          settings.refdata_snapshot.channel,
                                                                          settings.refdata_snapshot.stream_id);
    bus.trade_sub = std::make_unique<aeron::MdTradeSubscriber>(aeron,
                                                               settings.md_data.channel,
                                                               settings.md_data.stream_id);
    bus.bbo_sub = std::make_unique<aeron::MdMarketDataSubscriber>(aeron,
                                                                  settings.md_data.channel,
                                                                  settings.md_data.stream_id);
    bus.color_pub = std::make_unique<aeron::MarketColorPublisher>(aeron,
                                                                  settings.market_color.channel,
                                                                  settings.market_color.stream_id);
    return bus;
}

}  // namespace bpt::radar::messaging
