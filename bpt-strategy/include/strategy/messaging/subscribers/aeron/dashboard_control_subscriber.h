#pragma once

/// @file
/// Aeron-backed concrete for api::DashboardControlSubscriber.

#include "strategy/messaging/subscribers/api/dashboard_control_subscriber.h"

#include <Aeron.h>

#include <memory>
#include <string>

namespace bpt::strategy::messaging::aeron {

class DashboardControlSubscriber final : public api::DashboardControlSubscriber {
public:
    DashboardControlSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    [[nodiscard]] bool is_ready() const override { return static_cast<bool>(sub_); }

    int poll(int fragment_limit = 1) override;

private:
    std::shared_ptr<::aeron::Subscription> sub_;
};

}  // namespace bpt::strategy::messaging::aeron
