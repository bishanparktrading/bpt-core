#pragma once

/// \file
/// \brief Factory that maps an `ExchangeId` to its venue-specific `MdAdapter`.
///
/// The MD gateway supports N venues (Binance, OKX, Deribit, Hyperliquid); each
/// has its own adapter class but all conform to
/// bpt::md_gateway::adapter::IAdapter. This factory hides the per-venue switch
/// behind a single entry point so:
///   - MdGatewayService doesn't have to include every adapter header.
///   - bpt-tape (which reuses the same adapter code with a recording tee)
///     can call the same factory with a different publisher type.
///   - Adding a new venue is a one-file change here, not a service-wide edit.
///
/// The factory is templated on the publisher type (`Pub`):
///   - **Prod md-gateway** — `make_md_adapter<MdPublisher>(...)`
///   - **bpt-tape**       — `make_recording_adapter<NoopMdPublisher>(...)`
///     (separate factory in bpt-tape; same spirit)
///   - **Component tests** — `make_md_adapter<FakeMdPublisher>(...)`
///
/// The publisher type plumbs all the way through the templated decoder chain
/// (decoder → MdPublisher), so each call site only instantiates the adapter
/// variants it actually uses.
///
/// \see bpt::md_gateway::adapter::IAdapter
/// \see bpt::md_gateway::adapter::AdapterBase
/// \see bpt::md_gateway::messaging::MdPublisher
/// \see bpt::tape::adapter::make_recording_adapter — bpt-tape's mirror

#include "md_gateway/adapter/binance/binance_md_adapter.h"
#include "md_gateway/adapter/common/i_adapter.h"
#include "md_gateway/adapter/deribit/deribit_md_adapter.h"
#include "md_gateway/adapter/hyperliquid/hyperliquid_md_adapter.h"
#include "md_gateway/adapter/okx/okx_md_adapter.h"

#include <messages/ExchangeRegistry.h>

#include <memory>

namespace bpt::md_gateway::adapter {

/// \brief Build the venue-specific MdAdapter for the given exchange.
///
/// Returns a polymorphic `IAdapter` handle so the caller can wire it into
/// the SubscriptionManager, metric reporters, and connection-status
/// callbacks without knowing which concrete adapter class was
/// instantiated.
///
/// Two-stage validation pattern at the call site:
///   1. `ExchangeRegistry::from_name()` checks the TOML's `exchange`
///      string is in messages/exchanges.yaml. Catches typos.
///   2. This factory then checks mdgw has an adapter implementation
///      for that venue. Catches "registry knows it, adapter doesn't
///      exist yet."
///
/// \tparam Pub  The publisher type the adapter pipes normalised MD into.
///              Must satisfy `bpt::md_gateway::md::MdSink` (three
///              `publish()` overloads for MdBbo / MdTrade / MdOrderBook).
///              Prod uses `MdPublisher`; bpt-tape uses `NoopMdPublisher`;
///              tests use `FakeMdPublisher`.
///
/// \param exch_id  `ExchangeId` enum value, typically obtained from
///                 `ExchangeRegistry::from_name()` against the TOML's
///                 `exchange` string.
/// \param cfg      Per-venue adapter config block from the service TOML
///                 (`[[adapters]]` entry).
/// \param md_pub   The publisher instance the adapter pipes MD into.
///                 One `MdPublisher` per adapter — validator state is
///                 publisher-thread-confined (post-fold of
///                 ValidatingPublisher).
///
/// \return  `shared_ptr<IAdapter>` on success; `nullptr` when the
///          registry recognises the venue but mdgw has no adapter
///          implementation for it.
///
/// \note    Never throws. Translation of `nullptr` to an exception is a
///          deliberate caller responsibility so the thrown message can
///          include surrounding context (which `[[adapters]]` stanza
///          failed, the venue name, etc.).
///
/// Example:
/// \code
/// const auto exch_id = bpt::messages::ExchangeRegistry::from_name(a_cfg.exchange);
/// if (!exch_id)
///     throw std::runtime_error(fmt::format(
///         "Unknown exchange '{}' in mdgw config — not in messages/exchanges.yaml",
///         a_cfg.exchange));
///
/// auto md_pub = std::make_shared<messaging::MdPublisher>(aeron, channel, stream_id, ...);
/// auto adapter = adapter::make_md_adapter<messaging::MdPublisher>(*exch_id, a_cfg, md_pub);
/// if (!adapter)
///     throw std::runtime_error(fmt::format(
///         "Exchange '{}' is in the registry but mdgw has no adapter for it",
///         a_cfg.exchange));
/// \endcode
template <class Pub>
inline std::shared_ptr<IAdapter> make_md_adapter(
    bpt::messages::ExchangeId::Value exch_id,
    const bpt::md_gateway::config::AdapterConfig& cfg,
    std::shared_ptr<Pub> md_pub,
    std::shared_ptr<bpt::md_gateway::messaging::api::FundingRatePublisher> funding_pub,
    std::shared_ptr<bpt::md_gateway::messaging::api::InstrumentStatsPublisher> stats_pub) {
    using bpt::messages::ExchangeId;
    switch (exch_id) {
        case ExchangeId::BINANCE:
            return std::make_shared<BinanceMdAdapter<Pub>>(cfg, md_pub, funding_pub, stats_pub);
        case ExchangeId::OKX:
            return std::make_shared<OkxMdAdapter<Pub>>(cfg, md_pub, funding_pub, stats_pub);
        case ExchangeId::HYPERLIQUID:
            return std::make_shared<HyperliquidMdAdapter<Pub>>(cfg, md_pub, funding_pub, stats_pub);
        case ExchangeId::DERIBIT:
            return std::make_shared<DeribitMdAdapter<Pub>>(cfg, md_pub, funding_pub, stats_pub);
        default:
            return nullptr;
    }
}

}  // namespace bpt::md_gateway::adapter
