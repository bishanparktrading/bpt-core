#pragma once

/// @file
/// Port: console kill-switch / resume control subscriber.
/// 1-byte messages: 0x00 = HALT, 0x01 = RESUME. The bridge sends these
/// via the console_control stream when an operator clicks the
/// console button.
///
/// Strategies that just want subscribe/poll semantics hold an
/// `api::ConsoleControlSubscriber*`. The per-frame dispatch path was
/// lifted to a CRTP-templated concrete client — see
/// `aeron::ConsoleControlSubscriber<Handler>` (Handler is
/// `StrategyService` in prod). Removing the `std::function` callback
/// kills one indirection per command.

#include <cstdint>

namespace bpt::strategy::messaging::api {

class ConsoleControlSubscriber {
public:
    virtual ~ConsoleControlSubscriber() = default;

    /// True when the underlying subscription connected within the
    /// startup wait. False = subscription unavailable.
    [[nodiscard]] virtual bool is_ready() const = 0;

    virtual int poll(int fragment_limit = 1) = 0;
};

}  // namespace bpt::strategy::messaging::api
