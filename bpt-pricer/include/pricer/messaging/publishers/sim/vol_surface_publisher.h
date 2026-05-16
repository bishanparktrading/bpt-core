#pragma once

/// @file
/// Sim concrete of api::VolSurfacePublisher — header-only, no Aeron, no codec.
///
/// Dispatches the domain VolSurfaceGrid directly to a registered handler.
/// Intended for the deterministic backtester (bpt-backtester-mono), where
/// pricer + strategy + matching engine live in one process and there's
/// no wire to push bytes onto. Bypassing SBE encode + Aeron offer cuts
/// per-publish overhead from microseconds to a function-call hop.
///
/// Composition root for the sim case (sketch — future InProcessBus factory
/// in bpt-backtester-mono will own this):
/// \code
///   auto handler = [&strategy](const VolSurfaceGrid& g, uint64_t ts) {
///       strategy.on_vol_surface_direct(g, ts);   // no decode either
///   };
///   auto pub = std::make_unique<sim::VolSurfacePublisher>(handler);
///   // pub is stored in PricerBus as api::VolSurfacePublisher (upcast).
/// \endcode
///
/// Note: header-only because the implementation is trivial. If it grows
/// callbacks for replay-timestamp injection, latency injection, or
/// other test-time concerns, factor into a .cpp.

#include "pricer/messaging/publishers/api/vol_surface_publisher.h"
#include "pricer/surface/vol_surface_grid.h"

#include <cstdint>
#include <functional>
#include <utility>

namespace bpt::pricer::messaging::sim {

class VolSurfacePublisher : public api::VolSurfacePublisher {
public:
    using Handler = std::function<void(const surface::VolSurfaceGrid&, uint64_t timestamp_ns)>;

    /// Construct with a handler that will receive every publish() call.
    /// Handler is captured by value (typically a lambda); ownership of
    /// any state it closes over is the caller's responsibility.
    explicit VolSurfacePublisher(Handler handler) : handler_(std::move(handler)) {}

    void publish(const surface::VolSurfaceGrid& grid, uint64_t timestamp_ns) override {
        if (handler_)
            handler_(grid, timestamp_ns);
    }

private:
    Handler handler_;
};

}  // namespace bpt::pricer::messaging::sim
