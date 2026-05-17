#pragma once

/// \file
/// Aeron-backed concrete for api::MdSubscriber.

#include "pricer/md/api/md_subscriber.h"

#include <Aeron.h>

#include <bpt_common/aeron/subscriber.h>
#include <memory>
#include <string>

namespace bpt::pricer::md::aeron {

class MdSubscriber final : public api::MdSubscriber {
public:
    MdSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int32_t stream_id);

    int poll(int fragment_limit = 10) override;

private:
    void on_fragment(::aeron::AtomicBuffer& buffer,
                     ::aeron::util::index_t offset,
                     ::aeron::util::index_t length,
                     ::aeron::Header& header);

    std::unique_ptr<bpt::common::aeron::Subscriber> sub_;
};

}  // namespace bpt::pricer::md::aeron
