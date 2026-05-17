#pragma once

/// \file
/// Aeron-backed concrete for api::RefdataSubscriber. Reads snapshot
/// (stream 1001) and deltas (stream 1002) from bpt-refdata; can also
/// publish a control-stream snapshot request on demand.

#include "pricer/refdata/api/refdata_subscriber.h"

#include <Aeron.h>

#include <bpt_common/aeron/subscriber.h>
#include <cstdint>
#include <memory>
#include <string>

namespace bpt::pricer::refdata::aeron {

class RefdataSubscriber final : public api::RefdataSubscriber {
public:
    RefdataSubscriber(std::shared_ptr<::aeron::Aeron> aeron,
                      const std::string& snapshot_channel,
                      int32_t snapshot_stream_id,
                      const std::string& delta_channel,
                      int32_t delta_stream_id,
                      const std::string& control_channel = "",
                      int32_t control_stream_id = 0);

    void send_subscription_request(uint64_t correlation_id) override;

    int poll(int fragment_limit = 10) override;

private:
    void on_snapshot_fragment(::aeron::AtomicBuffer& buffer,
                              ::aeron::util::index_t offset,
                              ::aeron::util::index_t length,
                              ::aeron::Header& header);
    void on_delta_fragment(::aeron::AtomicBuffer& buffer,
                           ::aeron::util::index_t offset,
                           ::aeron::util::index_t length,
                           ::aeron::Header& header);

    std::unique_ptr<bpt::common::aeron::Subscriber> snapshot_sub_;
    std::unique_ptr<bpt::common::aeron::Subscriber> delta_sub_;
    // ctrl_pub_ intentionally raw — uses a 10s warmup deadline in
    // send_subscription_request, which doesn't fit the Publisher
    // helper's policy enum cleanly.
    std::shared_ptr<::aeron::Publication> ctrl_pub_;
};

}  // namespace bpt::pricer::refdata::aeron
