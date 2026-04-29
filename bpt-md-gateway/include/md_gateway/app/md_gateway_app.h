#pragma once

#include "md_gateway/adapter/common/i_adapter.h"
#include "md_gateway/config/settings.h"
#include "md_gateway/messaging/i_ack_publisher.h"
#include "md_gateway/messaging/i_funding_rate_publisher.h"
#include "md_gateway/messaging/i_md_control_source.h"
#include "md_gateway/messaging/md_publisher.h"
#include "md_gateway/metrics/metrics.h"
#include "md_gateway/subscription/subscription_manager.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <bpt_app/app.h>
#include <bpt_common/util/latency_histogram.h>
#include <bpt_common/util/topology.h>

namespace bpt::md_gateway {

class MdGatewayApp : public bpt::app::IService {
public:
    MdGatewayApp(config::Settings cfg,
                 std::unique_ptr<messaging::IMdControlSource> control_source,
                 std::shared_ptr<messaging::MdPublisher> md_sink,
                 std::unique_ptr<messaging::IAckPublisher> ack_sink,
                 std::shared_ptr<messaging::IFundingRatePublisher> funding_sink,
                 const bpt::common::util::Topology& topology);
    void run() override;
    void stop() override;

private:
    config::Settings cfg_;
    metrics::MdGatewayMetrics metrics_;
    std::shared_ptr<messaging::MdPublisher> md_pub_;
    std::shared_ptr<messaging::IFundingRatePublisher> funding_pub_;
    std::unique_ptr<messaging::IAckPublisher> ack_pub_;
    std::unique_ptr<messaging::IMdControlSource> ctrl_sub_;
    subscription::SubscriptionManager sub_mgr_;

    // Collected at construction; used by the periodic latency reporter in run().
    std::vector<std::pair<std::string, bpt::common::util::LatencyHistogram*>> lat_reporters_;
    std::vector<std::pair<std::string, adapter::IAdapter*>> md_stat_reporters_;
};

}  // namespace bpt::md_gateway
