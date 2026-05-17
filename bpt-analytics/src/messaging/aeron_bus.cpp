#include "analytics/messaging/aeron_bus.h"

#include "analytics/config/settings.h"
#include "analytics/messaging/publishers/aeron/toxicity_publisher.h"
#include "analytics/messaging/subscribers/aeron/exec_report_subscriber.h"
#include "analytics/messaging/subscribers/aeron/md_bbo_subscriber.h"

namespace bpt::analytics::messaging {

AnalyticsBus AnalyticsAeronBus::build(std::shared_ptr<::aeron::Aeron> aeron, const config::Settings& settings) {
    AnalyticsBus bus;
    bus.exec_sub = std::make_unique<aeron::ExecReportSubscriber>(aeron,
                                                                 settings.exec_report.channel,
                                                                 settings.exec_report.stream_id);
    bus.md_sub = std::make_unique<aeron::MdBboSubscriber>(aeron,
                                                          settings.md_data.channel,
                                                          settings.md_data.stream_id);
    bus.tox_pub =
        std::make_unique<aeron::ToxicityPublisher>(aeron, settings.toxicity.channel, settings.toxicity.stream_id);
    return bus;
}

}  // namespace bpt::analytics::messaging
