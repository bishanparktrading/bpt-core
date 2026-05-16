#include "backtester/messaging/aeron_bus.h"

#include "backtester/config/settings.h"
#include "backtester/messaging/publishers/aeron/backtest_control_publisher.h"
#include "backtester/messaging/subscribers/aeron/backtest_ack_subscriber.h"

#include <bpt_common/aeron/aeron_utils.h>

namespace bpt::backtester::messaging {

BacktesterBus BacktesterAeronBus::build(std::shared_ptr<::aeron::Aeron> aeron, const config::Settings& settings) {
    const auto& ac = settings.aeron;
    auto pub =
        bpt::common::aeron::wait_for_publication(aeron, ac.backtest_control.channel, ac.backtest_control.stream_id);
    auto sub = bpt::common::aeron::wait_for_subscription(aeron, ac.backtest_ack.channel, ac.backtest_ack.stream_id);

    BacktesterBus bus;
    bus.ctrl_pub = std::make_unique<aeron::BacktestControlPublisher>(std::move(pub));
    bus.ack_sub = std::make_unique<aeron::BacktestAckSubscriber>(std::move(sub));
    return bus;
}

}  // namespace bpt::backtester::messaging
