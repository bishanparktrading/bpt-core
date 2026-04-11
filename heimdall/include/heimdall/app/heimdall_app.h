#pragma once

#include "heimdall/adapter/common/credentials.h"
#include "heimdall/adapter/common/i_order_adapter.h"
#include "heimdall/config/settings.h"
#include "heimdall/messaging/account_snapshot_publisher.h"
#include "heimdall/messaging/exec_report_publisher.h"
#include "heimdall/messaging/heartbeat_publisher.h"
#include "heimdall/messaging/order_subscriber.h"
#include "heimdall/metrics/metrics.h"
#include "heimdall/order/order_processor.h"
#include "heimdall/order/order_state_manager.h"
#include "heimdall/risk/risk_checker.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace heimdall {

class HeimdallApp {
public:
    HeimdallApp(config::Settings cfg,
                std::shared_ptr<aeron::Aeron> aeron,
                std::map<std::string, adapter::ExchangeCredentials> creds);
    void run();

private:
    config::Settings cfg_;
    std::shared_ptr<aeron::Aeron> aeron_;
    metrics::HeimdallMetrics metrics_;
    std::shared_ptr<messaging::ExecReportPublisher> exec_pub_;
    std::shared_ptr<messaging::AccountSnapshotPublisher> account_snap_pub_;
    std::shared_ptr<messaging::HeartbeatPublisher> hb_pub_;
    std::shared_ptr<messaging::OrderSubscriber> order_sub_;
    risk::RiskChecker risk_checker_;
    order::OrderStateManager state_mgr_;
    std::vector<std::shared_ptr<adapter::IOrderAdapter>> adapters_;
    std::unique_ptr<order::OrderProcessor> processor_;
};

}  // namespace heimdall
