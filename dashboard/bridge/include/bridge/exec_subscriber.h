#pragma once

#include "bridge/message_encoder.h"

#include <Aeron.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace bridge {

// Subscribes to Heimdall's exec report stream and delivers decoded fills.
// Only FILLED / PARTIALLY_FILLED statuses are forwarded; rejects, cancels,
// acks, and other lifecycle events are ignored.
class ExecSubscriber {
public:
    struct Fill {
        uint64_t       ts_ns;
        uint64_t       order_id;
        uint64_t       instrument_id;
        encode::Side   side;
        double         qty;     // natural units
        double         price;   // natural units
    };

    using FillHandler = std::function<void(const Fill&)>;

    ExecSubscriber(std::shared_ptr<aeron::Aeron> aeron,
                   const std::string& channel,
                   int32_t stream_id);

    void set_handler(FillHandler h) { handler_ = std::move(h); }

    int poll(int fragment_limit = 32);

private:
    void on_fragment(const aeron::concurrent::AtomicBuffer& buffer,
                     aeron::util::index_t offset,
                     aeron::util::index_t length,
                     const aeron::Header& header);

    std::shared_ptr<aeron::Subscription> sub_;
    FillHandler handler_;
};

}  // namespace bridge
