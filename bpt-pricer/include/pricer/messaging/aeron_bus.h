#pragma once

/// @file
/// Bus boundary for bpt-pricer. Mirrors the shape used by bpt-refdata,
/// bpt-md-gateway, bpt-order-gateway, and bpt-strategy: every concrete
/// Aeron pub/sub the pricer needs is constructed in one factory so
/// `PricerService` doesn't have to take `<Aeron.h>` in its constructor.
///
/// VolSurfacePublisher and StatusPublisher are promoted to ports
/// (api::VolSurfacePublisher / api::StatusPublisher with aeron::* prod
/// concretes) so the deterministic backtester can substitute the
/// codec-bypass sim::VolSurfacePublisher. Off-hot-path vtable cost is
/// invisible at the ~Hz cadence of vol-surface rebuilds. The remaining
/// publishers + subscribers stay as concrete classes — promote each
/// individually when a non-Aeron consumer materialises (same pragmatic
/// call as bpt-strategy / bpt-analytics).

#include "pricer/md/md_subscribe_client.h"
#include "pricer/md/md_subscriber.h"
#include "pricer/messaging/publishers/api/status_publisher.h"
#include "pricer/messaging/publishers/api/vol_surface_publisher.h"
#include "pricer/refdata/refdata_subscriber.h"

#include <Aeron.h>

#include <memory>

namespace bpt::pricer {
namespace config {
struct Settings;
}

namespace messaging {

struct PricerBus {
    std::unique_ptr<api::VolSurfacePublisher> vol_pub;     ///< port; aeron::VolSurfacePublisher in prod
    std::unique_ptr<api::StatusPublisher>     status_pub;  ///< port; aeron::StatusPublisher in prod
    std::unique_ptr<md::MdSubscriber> md_sub;
    std::unique_ptr<md::MdSubscribeClient> md_ctrl;  ///< pricer → md-gateway: subscribe batches
    std::unique_ptr<refdata::RefdataSubscriber> refdata_sub;
};

class PricerAeronBus {
public:
    /// Build every Aeron-touching object the pricer needs. Sole place
    /// that calls into `<Aeron.h>` from the application layer.
    static PricerBus build(std::shared_ptr<::aeron::Aeron> aeron, const config::Settings& settings);
};

}  // namespace messaging
}  // namespace bpt::pricer
