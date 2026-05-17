#pragma once

/// \file
/// Aeron concrete: subscribes to bpt-pricer's VolSurface stream
/// (typically 4001). Surface messages can span multiple Aeron fragments
/// — uses FragmentAssembler to reassemble.

#include "radar/messaging/subscribers/api/vol_surface_subscriber.h"

#include <Aeron.h>
#include <FragmentAssembler.h>

#include <memory>
#include <string>

namespace bpt::radar::messaging::aeron {

class VolSurfaceSubscriber final : public api::VolSurfaceSubscriber {
public:
    VolSurfaceSubscriber(std::shared_ptr<::aeron::Aeron> aeron, const std::string& channel, int stream_id);

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
