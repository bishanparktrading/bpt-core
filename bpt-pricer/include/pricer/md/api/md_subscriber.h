#pragma once

/// \file
/// Port: passive MD subscriber. Reads BBO + trade ticks from md-gateway's
/// stream 2002. Pricer is a read-only consumer; it does not send
/// subscription requests through this port (see api::MdSubscribeClient
/// for the control side).

#include <cstdint>
#include <functional>

namespace bpt::pricer::md::api {

class MdSubscriber {
public:
    using BboCallback = std::function<void(uint64_t instrument_id, double bid, double ask, uint64_t timestamp_ns)>;
    using TradeCallback = std::function<void(uint64_t instrument_id, double price, double qty, uint64_t timestamp_ns)>;

    virtual ~MdSubscriber() = default;

    void set_bbo_callback(BboCallback cb) { on_bbo_ = std::move(cb); }
    void set_trade_callback(TradeCallback cb) { on_trade_ = std::move(cb); }

    /// Poll for fragments. Returns number of fragments processed.
    virtual int poll(int fragment_limit = 10) = 0;

protected:
    BboCallback on_bbo_;
    TradeCallback on_trade_;
};

}  // namespace bpt::pricer::md::api
