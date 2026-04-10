#pragma once

#include "fenrir/refdata/instrument_cache.h"

#include <bifrost_protocol/AccountSnapshot.h>
#include <bifrost_protocol/DeltaUpdateType.h>
#include <bifrost_protocol/ExecutionReport.h>
#include <bifrost_protocol/MdMarketData.h>
#include <bifrost_protocol/MdOrderBook.h>
#include <bifrost_protocol/MdTrade.h>
#include <bifrost_protocol/VolSurface.h>

namespace fenrir::strategy {

class IStrategy {
public:
    virtual ~IStrategy() = default;

    // Activates the strategy
    virtual void start() = 0;

    // Callbacks from RefdataClient
    virtual void on_snapshot(const refdata::InstrumentCache& cache) = 0;
    virtual void on_delta(const refdata::Instrument& inst, bifrost::protocol::DeltaUpdateType::Value update_type) = 0;

    // Callbacks from MdClient
    virtual void on_bbo(const bifrost::protocol::MdMarketData& tick) = 0;
    virtual void on_trade(const bifrost::protocol::MdTrade& tick) = 0;

    // Fired when Huginn is configured with order_book_depth > 0.
    // Default no-op — only market-making strategies need to override this.
    virtual void on_order_book(const bifrost::protocol::MdOrderBook& /*book*/) {}

    // Fired when Surtr publishes a new vol surface snapshot.
    // Non-const ref: SBE group iterators are stateful and require mutation.
    // Default no-op — only options strategies need to override this.
    virtual void on_vol_surface(bifrost::protocol::VolSurface& /*surface*/) {}

    // Fired for every execution report from Heimdall.
    // Default no-op — strategies that manage positions must override this.
    virtual void on_exec_report(const bifrost::protocol::ExecutionReport& /*rpt*/) {}

    // Fired once per exchange at startup when the account snapshot is received.
    // Non-const ref: SBE group iterators are stateful.
    // Default no-op — strategies that need startup position seeding should override this.
    virtual void on_account_snapshot(bifrost::protocol::AccountSnapshot& /*snap*/) {}
};

}  // namespace fenrir::strategy
