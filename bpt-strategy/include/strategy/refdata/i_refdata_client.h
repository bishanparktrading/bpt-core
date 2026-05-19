#pragma once

/// @file
/// IRefdataClient — abstract interface for the strategy's refdata client.
/// Implementations:
///   AeronRefdataClient<Handler>     — production path; consumes refdata
///                                      snapshots, deltas, fees, funding,
///                                      and status over Aeron.
///   InProcessRefdataClient<Handler> — deterministic backtest path; loads
///                                      the refdata snapshot from JSON
///                                      synchronously.
///
/// Strategy code holds an IRefdataClient&; the bus factory injects
/// whichever implementation matches the run mode. The per-frame
/// dispatch path was lifted to a CRTP-templated concrete client so the
/// optimiser sees the full chain (fragment → handler) without
/// std::function indirection.
///
/// The Handler is required to define:
///   void on_refdata_ready(uint8_t exchanges, uint16_t inst_count,
///                         bool fees_loaded, bool funding_loaded);
///   void on_refdata_error(RefDataErrorType::Value, ExchangeId::Value,
///                         uint64_t instrument_id);
///   void on_refdata_snapshot_complete(const InstrumentCache&);
///   void on_refdata_delta(const Instrument&, DeltaUpdateType::Value);
///   void on_refdata_gap_detected();
///
/// In prod the Handler is `StrategyService`.

#include "strategy/refdata/fee_cache.h"
#include "strategy/refdata/funding_rate_cache.h"
#include "strategy/refdata/instrument.h"
#include "strategy/refdata/instrument_cache.h"

#include <messages/InstrumentType.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bpt::strategy::refdata {

class IRefdataClient {
public:
    /// Canonical filter entry to pre-filter the snapshot server-side.
    /// An empty exchange means "any exchange".
    struct CanonicalFilter {
        std::string base;
        std::string quote;
        bpt::messages::InstrumentType::Value instrument_type;
        std::string exchange;
    };

    virtual ~IRefdataClient() = default;

    /// Send a subscription request with canonical filters so the server
    /// pre-filters the snapshot. An empty filters vector means subscribe-all
    /// (receive the full universe).
    virtual void subscribe(uint64_t correlation_id, std::vector<CanonicalFilter> filters = {}) = 0;

    /// Poll all streams. Returns total fragment count processed.
    /// Backtest impls may return 0 (no polling).
    virtual int poll(int fragment_limit = 10) = 0;

    /// Nanosecond timestamp of the last heartbeat received (0 if none yet).
    [[nodiscard]] virtual uint64_t last_heartbeat_ns() const = 0;

    /// Cache accessors. The caches are populated by the implementation;
    /// callers read through these refs.
    [[nodiscard]] virtual const InstrumentCache& cache() const = 0;
    [[nodiscard]] virtual const FeeCache& fee_cache() const = 0;
    [[nodiscard]] virtual const FundingRateCache& funding_rate_cache() const = 0;
};

}  // namespace bpt::strategy::refdata
