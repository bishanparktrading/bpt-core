#pragma once

/// @file
/// Port: dashboard kill-switch / resume control subscriber.
/// 1-byte messages: 0x00 = HALT, 0x01 = RESUME. The bridge sends these
/// via the console_control stream when an operator clicks the
/// dashboard button. Strategy translates HALT into trading_halted_=true
/// and stops sending orders. Aeron concrete in
/// `aeron/console_control_subscriber.h`.

#include <cstdint>
#include <functional>

namespace bpt::strategy::messaging::api {

class ConsoleControlSubscriber {
public:
    using OnCommandFn = std::function<void(uint8_t cmd)>;

    virtual ~ConsoleControlSubscriber() = default;

    /// True when the underlying subscription connected within the
    /// startup wait. False = subscription unavailable.
    [[nodiscard]] virtual bool is_ready() const = 0;

    virtual int poll(int fragment_limit = 1) = 0;

    OnCommandFn on_command;
};

}  // namespace bpt::strategy::messaging::api
