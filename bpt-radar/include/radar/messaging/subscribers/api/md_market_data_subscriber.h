#pragma once

/// \file
/// Port: BBO subscriber (md_data stream, MdMarketData fragments only).
/// Aeron concrete in `aeron/md_market_data_subscriber.h`.

#include <messages/MdMarketData.h>

#include <functional>

namespace bpt::radar::messaging::api {

class MdMarketDataSubscriber {
public:
    using OnBboFn = std::function<void(bpt::messages::MdMarketData&)>;

    virtual ~MdMarketDataSubscriber() = default;

    virtual int poll(int fragment_limit = 16) = 0;

    OnBboFn on_bbo;
};

}  // namespace bpt::radar::messaging::api
