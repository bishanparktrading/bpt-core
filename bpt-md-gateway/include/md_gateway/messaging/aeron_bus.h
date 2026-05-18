#pragma once

/// \file
/// \brief Composition root that wires the Aeron-backed implementations of the messaging ports.
///
/// MdGatewayService talks to four messaging ports (api::MdControlSubscriber +
/// api::AckPublisher + api::FundingRatePublisher + api::InstrumentStatsPublisher)
/// without knowing how they are implemented. `MdGatewayAeronBus::build()` is
/// the single place that constructs the Aeron-backed concrete classes and
/// bundles them into a struct the app accepts in its constructor — swap
/// this factory for a different one (e.g. an in-memory bus for seam tests)
/// and the app code is unchanged.
///
/// The MD publisher (hot path) is intentionally NOT a bus field: each
/// venue adapter owns its own MdPublisher (one per publisher thread, so
/// validator+breaker state stays thread-confined). The service constructs
/// them inline against the shared `aeron` client and `settings.aeron.md_data`.
///
/// Lifetime: MdGatewayBus owns the publisher and subscriber objects but
/// hands ownership to MdGatewayService at construction; MdGatewayBus itself is
/// a value type that can be moved out at the wiring site.
///
/// Where this bus sits in the service stack:
///
///     [ main.cpp ]                              composition root
///         ↓
///     [ MdGatewayService ]                      service / poll loop
///         ↓ owns
///     [ MdGatewayBus ]  ←── this file                bus (messaging composition root)
///         │
///         └──→ [ api::*Publisher / Subscriber ] virtual ports (slow path)
///                  ↓ dispatches to
///              [ aeron::*Publisher / Subscriber ] concretes (own Codec<C,T>)
///
///     [ MdPublisher ] (hot path)                    per-adapter; service-built
///
/// See docs/service-anatomy.md for the full layered shape.

#include "md_gateway/messaging/publishers/api/ack_publisher.h"
#include "md_gateway/messaging/publishers/api/funding_rate_publisher.h"
#include "md_gateway/messaging/publishers/api/instrument_stats_publisher.h"
#include "md_gateway/messaging/subscribers/api/md_control_subscriber.h"

#include <Aeron.h>

#include <memory>

namespace bpt::md_gateway::config {
struct Settings;
}

namespace bpt::md_gateway::messaging {

/// \brief Bundle of messaging-port implementations handed to MdGatewayService.
///
/// Each field is one port, all exposed via interface type so that
/// alternate implementations (test fakes, recorder no-ops) can substitute
/// without rebuilding the app. The MD data publisher is intentionally
/// absent — the service builds one per venue adapter (see file-level doc).
struct MdGatewayBus {
    /// \brief Inbound: SBE `MdSubscribeBatch` control fragments from strategy.
    ///
    /// Polled from MdGatewayService::run(); each fragment dispatched to the
    /// SubscriptionManager.
    std::unique_ptr<api::MdControlSubscriber> control_sub;

    /// \brief Outbound: subscription ACKs + service heartbeats to strategy.
    std::unique_ptr<api::AckPublisher> ack_pub;

    /// \brief Outbound: per-instrument funding-rate updates on stream 1005.
    ///
    /// Wired into each adapter's `on_funding_rate` callback by MdGatewayService;
    /// adapter threads call publish() directly off their IO thread.
    std::shared_ptr<api::FundingRatePublisher> funding_pub;

    /// \brief Outbound: per-instrument stats updates on stream 2004.
    ///
    /// Wired into each adapter's `on_instrument_stats` callback by MdGatewayService.
    /// Slow-cadence (updates every few seconds) — kept off the BBO firehose so
    /// strategy consumers that don't need OI don't pay decode cost on every tick.
    std::shared_ptr<api::InstrumentStatsPublisher> stats_pub;

};

class MdGatewayAeronBus {
public:
    /// \brief Construct the prod (Aeron-backed) implementations of the ports.
    ///
    /// Reads channel + stream-id assignments from `settings.aeron`. The
    /// supplied `aeron` shared client must already have a MediaDriver
    /// connection — see bpt::app::run() which sets it up.
    static MdGatewayBus build(std::shared_ptr<::aeron::Aeron> aeron, const config::Settings& settings);
};

}  // namespace bpt::md_gateway::messaging
