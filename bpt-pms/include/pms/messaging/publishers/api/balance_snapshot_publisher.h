#pragma once

/// @file
/// Port for the PMS balance-snapshot stream. PmsService publishes a
/// BalanceSnapshot per adapter poll; aeron concrete is the Aeron+SBE
/// implementation, a sim variant would dispatch the struct directly to
/// whatever consumer holds the snapshot.

#include "pms/adapter/balance_row.h"

namespace bpt::pms::messaging::api {

class BalanceSnapshotPublisher {
public:
    virtual ~BalanceSnapshotPublisher() = default;
    virtual void publish(const adapter::BalanceSnapshot& snapshot) = 0;
};

}  // namespace bpt::pms::messaging::api
