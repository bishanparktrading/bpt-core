#pragma once

/// @file
/// Port: bpt-analytics toxicity stream subscriber. The per-message
/// dispatch path was lifted to a CRTP-templated concrete subscriber —
/// see `aeron::ToxicitySubscriber<Handler>` (Handler is `StrategyService`
/// in prod). Removing the `std::function` callback kills one
/// indirection per update.

namespace bpt::strategy::messaging::api {

class ToxicitySubscriber {
public:
    virtual ~ToxicitySubscriber() = default;

    virtual int poll(int fragment_limit = 4) = 0;
};

}  // namespace bpt::strategy::messaging::api
