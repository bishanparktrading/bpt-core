#pragma once

/// @file
/// Port: BBO subscriber. Decodes SBE `MdMarketData` and dispatches via
/// the `on_bbo` std::function callback (domain-shaped: instrument_id,
/// bid, ask, timestamp_ns). The Aeron concrete in
/// `aeron/md_bbo_subscriber.h` is the prod implementation.

#include <cstdint>
#include <functional>

namespace bpt::analytics::messaging::api {

class MdBboSubscriber {
public:
    using OnBboFn = std::function<void(uint64_t instrument_id, double bid, double ask, uint64_t timestamp_ns)>;

    virtual ~MdBboSubscriber() = default;

    virtual int poll(int fragment_limit = 10) = 0;

    OnBboFn on_bbo;
};

}  // namespace bpt::analytics::messaging::api
