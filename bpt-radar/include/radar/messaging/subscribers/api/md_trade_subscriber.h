#pragma once

/// \file
/// Port: trade subscriber (md_data stream, MdTrade fragments only).
/// Aeron concrete in `aeron/md_trade_subscriber.h`.

#include <messages/MdTrade.h>

#include <functional>

namespace bpt::radar::messaging::api {

class MdTradeSubscriber {
public:
    using OnTradeFn = std::function<void(bpt::messages::MdTrade&)>;

    virtual ~MdTradeSubscriber() = default;

    virtual int poll(int fragment_limit = 16) = 0;

    OnTradeFn on_trade;
};

}  // namespace bpt::radar::messaging::api
