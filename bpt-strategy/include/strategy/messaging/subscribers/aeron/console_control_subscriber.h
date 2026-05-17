#pragma once

/// @file
/// Aeron-backed concrete for api::ConsoleControlSubscriber.

#include "strategy/messaging/subscribers/api/console_control_subscriber.h"

#include <Aeron.h>

#include <memory>
#include <string>

namespace bpt::strategy::messaging::aeron {

class ConsoleControlSubscriber final : public api::ConsoleControlSubscriber {
public:
    ConsoleControlSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    [[nodiscard]] bool is_ready() const override { return static_cast<bool>(sub_); }

    int poll(int fragment_limit = 1) override;

private:
    std::shared_ptr<::aeron::Subscription> sub_;
};

}  // namespace bpt::strategy::messaging::aeron
