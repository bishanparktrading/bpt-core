#pragma once

/// \file
/// \brief Composition root that wires the Aeron-backed implementations of the messaging ports.
///
/// MdGatewayService talks to two messaging ports (api::MdControlSubscriber +
/// api::AckPublisher) without knowing how they're implemented.
/// `MdGatewayAeronBus::build()` is the single place that constructs the
/// Aeron-backed concrete classes and bundles them into a struct the app
/// accepts in its constructor — swap this factory for a different one
/// (e.g. an in-memory bus for seam tests) and the app code is unchanged.
///
/// What the bus does NOT carry (greenfield: adapter owns what it produces):
///   - MdPublisher        (hot path; per-adapter, service-built inline)
///   - FundingRatePublisher  (slow path; per-adapter, service-built inline)
///   - InstrumentStatsPublisher (slow path; per-adapter, service-built inline)
///
/// Each venue adapter constructs its own publications on those streams.
/// Aeron handles N publications on one stream natively (multi-session).
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
