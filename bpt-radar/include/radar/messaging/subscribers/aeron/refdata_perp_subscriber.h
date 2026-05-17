#pragma once

/// \file
/// Aeron concrete: refdata-snapshot consumer scoped to perpetual
/// instruments only. Reassembles multi-fragment snapshots with
/// FragmentAssembler.

#include "radar/messaging/subscribers/api/refdata_perp_subscriber.h"

#include <Aeron.h>
#include <FragmentAssembler.h>

#include <memory>
#include <string>

namespace bpt::radar::messaging::aeron {

class RefdataPerpSubscriber final : public api::RefdataPerpSubscriber {
public:
    RefdataPerpSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

    int poll(int fragment_limit = 4) override;

private:
    void handle_fragment(::aeron::AtomicBuffer& buffer,
                         ::aeron::util::index_t offset,
                         ::aeron::util::index_t length,
                         ::aeron::Header& header);

    std::shared_ptr<::aeron::Subscription> sub_;
    std::unique_ptr<::aeron::FragmentAssembler> assembler_;
};

}  // namespace bpt::radar::messaging::aeron
