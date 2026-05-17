#pragma once

/// @file
/// Aeron-backed concrete for api::ExecReportSubscriber. Subscribes to
/// the order-gateway exec-report stream and decodes SBE `ExecutionReport`
/// fragments before dispatching to the `on_report` callback.

#include "analytics/messaging/subscribers/api/exec_report_subscriber.h"

#include <bpt_common/aeron/subscriber.h>
#include <memory>
#include <string>

namespace bpt::analytics::messaging::aeron {

class ExecReportSubscriber final : public api::ExecReportSubscriber {
public:
    ExecReportSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    int poll(int fragment_limit = 10) override;

private:
    std::unique_ptr<bpt::common::aeron::Subscriber> sub_;
};

}  // namespace bpt::analytics::messaging::aeron
