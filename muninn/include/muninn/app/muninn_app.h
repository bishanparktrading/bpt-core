#pragma once

#include "muninn/adapter/common/i_exchange_refdata_adapter.h"
#include "muninn/adapter/credentials.h"
#include "muninn/config/settings.h"
#include "muninn/mapping/instrument_mapping_loader.h"
#include "muninn/mapping/instrument_mapping_s3_fetcher.h"
#include "muninn/messaging/fee_schedule_publisher.h"
#include "muninn/messaging/muninn_status_publisher.h"
#include "muninn/messaging/refdata_control_subscriber.h"
#include "muninn/messaging/refdata_delta_publisher.h"
#include "muninn/messaging/refdata_snapshot_publisher.h"
#include "muninn/messaging/subscription_manager.h"
#include "muninn/metrics/metrics.h"
#include "muninn/registry/instrument_registry.h"

#include <Aeron.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace muninn {

class MuninnApp {
public:
    MuninnApp(config::Settings settings,
              std::shared_ptr<aeron::Aeron> aeron,
              std::map<std::string, adapter::ExchangeCredentials> creds);
    void run();

private:
    config::Settings settings_;
    std::shared_ptr<aeron::Aeron> aeron_;
    metrics::MuninnMetrics metrics_;
    std::shared_ptr<mapping::InstrumentMappingLoader> instrument_mapping_;
    std::optional<mapping::InstrumentMappingS3Fetcher> s3_fetcher_;
    std::shared_ptr<registry::InstrumentRegistry> registry_;
    std::unique_ptr<messaging::RefdataControlSubscriber> control_sub_;
    std::unique_ptr<messaging::RefdataSnapshotPublisher> snapshot_pub_;
    std::shared_ptr<messaging::RefdataDeltaPublisher> delta_pub_;
    std::shared_ptr<messaging::FeeSchedulePublisher> fee_pub_;
    std::shared_ptr<messaging::MuninnStatusPublisher> status_pub_;
    std::vector<std::unique_ptr<adapter::IExchangeRefDataAdapter>> adapters_;
    messaging::SubscriptionManager sub_manager_;
    std::mutex pub_mutex_;  // Guards publisher calls during parallel snapshot fetch
};

}  // namespace muninn
