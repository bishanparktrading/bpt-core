#pragma once

/// @file
/// Port for the PMS balance-snapshot stream. PmsService publishes a
/// BalanceSnapshot per adapter poll; concrete is the Aeron+SBE
/// implementation, in-process variant would dispatch the struct
/// directly to whatever consumer holds the snapshot.

#include "pms/adapter/balance_row.h"

namespace bpt::pms::messaging {

class IBalanceSnapshotPublisher {
public:
    virtual ~IBalanceSnapshotPublisher() = default;
    virtual void publish(const adapter::BalanceSnapshot& snapshot) = 0;
};

}  // namespace bpt::pms::messaging
