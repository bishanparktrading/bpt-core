#pragma once

/// \file
/// Aeron concrete: subscribes to bpt-md-gateway's md_data stream
/// (typically 2002) and fans out MdMarketData (BBO) fragments only.
/// Sibling of aeron::MdTradeSubscriber — both attach to the same stream
/// and filter by templateId.

#include "radar/messaging/subscribers/api/md_market_data_subscriber.h"

#include <Aeron.h>

#include <memory>
#include <string>

namespace bpt::radar::messaging::aeron {

class MdMarketDataSubscriber final : public api::MdMarketDataSubscriber {
public:
    MdMarketDataSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    int poll(int fragment_limit = 16) override;

private:
    void handle_fragment(::aeron::AtomicBuffer& buffer,
                         ::aeron::util::index_t offset,
                         ::aeron::util::index_t length,
                         ::aeron::Header& header);

    std::shared_ptr<::aeron::Subscription> sub_;
};

}  // namespace bpt::radar::messaging::aeron
