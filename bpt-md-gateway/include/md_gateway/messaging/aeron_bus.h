#pragma once

/// \file
/// \brief Composition root for the Aeron-backed bus adapters.

#include "md_gateway/messaging/i_ack_publisher.h"
#include "md_gateway/messaging/i_funding_rate_publisher.h"
#include "md_gateway/messaging/i_md_control_source.h"
#include "md_gateway/messaging/md_publisher.h"

#include <Aeron.h>

#include <memory>

namespace bpt::md_gateway::config {
struct Settings;
}

namespace bpt::md_gateway::messaging {

struct AeronBus {
    std::unique_ptr<IMdControlSource> control_source;
    /// Concrete publisher type — venue adapters are templated on Pub so
    /// the publish() chain inlines down to the wire. Swappable at the
    /// composition root by instantiating the templated adapters with a
    /// different Pub (e.g. md-recorder uses NoopMdPublisher).
    std::shared_ptr<MdPublisher> md_sink;
    std::unique_ptr<IAckPublisher> ack_sink;
    std::shared_ptr<IFundingRatePublisher> funding_sink;

    static AeronBus build(std::shared_ptr<aeron::Aeron> aeron,
                          const config::Settings& settings);
};

}  // namespace bpt::md_gateway::messaging
