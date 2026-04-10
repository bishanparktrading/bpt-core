#pragma once

#include "huginn/adapter/common/i_adapter.h"
#include "huginn/config/settings.h"
#include "huginn/messaging/ack_publisher.h"
#include "huginn/messaging/funding_rate_publisher.h"
#include "huginn/messaging/md_publisher.h"
#include "huginn/metrics/metrics.h"
#include "huginn/subscription/subscription_manager.h"

#include <Aeron.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <yggdrasil/util/latency_histogram.h>

namespace huginn {

class HuginnApp {
public:
    HuginnApp(config::Settings cfg, std::shared_ptr<aeron::Aeron> aeron);
    void run();

private:
    config::Settings cfg_;
    std::shared_ptr<aeron::Aeron> aeron_;
    metrics::HuginnMetrics metrics_;
    std::shared_ptr<messaging::MdPublisher> md_pub_;
    std::shared_ptr<messaging::FundingRatePublisher> funding_pub_;
    messaging::AckPublisher ack_pub_;
    std::shared_ptr<aeron::Subscription> ctrl_sub_;
    subscription::SubscriptionManager sub_mgr_;

    // Collected at construction; used by the periodic latency reporter in run().
    std::vector<std::pair<std::string, ygg::util::LatencyHistogram*>> lat_reporters_;
    std::vector<std::pair<std::string, adapter::IAdapter*>> md_stat_reporters_;
};

}  // namespace huginn
