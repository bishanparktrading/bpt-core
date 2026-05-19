#pragma once

/// \file
/// \brief Outbound port: AccountSnapshot publish toward strategy / console.
///
/// AccountSnapshot is the periodic + on-demand picture of the account
/// state at a venue (positions, currency balances, available margin).
/// Published on its own Aeron stream so subscribers (strategy gating
/// shutdown_flatten, console HoldingsPanel) can attach without
/// touching the higher-rate exec_report stream.
///
/// Implementations: aeron::AccountSnapshotPublisher in prod; fakes in
/// unit tests if/when needed.

#include "order_gateway/adapter/common/account_snapshot_data.h"

namespace bpt::order_gateway::messaging::api {

/// \brief Contract for the AccountSnapshot outbound port.
///
/// Called from a single dedicated executor thread (`AccountSnapExecutor`),
/// not the poll loop. The executor serialises all fetch + publish calls
/// across adapters, so implementations do NOT need to be thread-safe for
/// concurrent multi-writer use.
class AccountSnapshotPublisher {
public:
    virtual ~AccountSnapshotPublisher() = default;

    /// \brief Encode and publish one snapshot. May log + drop on
    ///        no-subscriber rather than block.
    virtual void publish(const adapter::AccountSnapshotData& snapshot) = 0;
};

}  // namespace bpt::order_gateway::messaging::api
