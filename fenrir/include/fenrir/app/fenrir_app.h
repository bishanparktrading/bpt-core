#pragma once

#include "fenrir/backtest/backtest_client.h"
#include "fenrir/config/config.h"
#include "fenrir/md/md_client.h"
#include "fenrir/metrics/metrics.h"
#include "fenrir/order/order_gateway_client.h"
#include "fenrir/order/order_manager.h"
#include "fenrir/refdata/fee_cache.h"
#include "fenrir/refdata/funding_rate_cache.h"
#include "fenrir/refdata/refdata_client.h"
#include "fenrir/strategy/i_strategy.h"
#include "fenrir/vol/vol_surface_client.h"

#include <Aeron.h>

#include <memory>

namespace fenrir {

class FenrirApp {
public:
    FenrirApp(config::AppConfig cfg, std::shared_ptr<aeron::Aeron> aeron);
    void run();

private:
    void wire_callbacks();
    void run_backtest_loop();
    void check_service_liveness();

    config::AppConfig cfg_;
    std::shared_ptr<aeron::Aeron> aeron_;
    metrics::FenrirMetrics metrics_;
    refdata::FeeCache fee_cache_;
    refdata::FundingRateCache funding_rate_cache_;
    std::unique_ptr<refdata::RefdataClient> refdata_;
    std::unique_ptr<md::MdClient> md_client_;
    std::unique_ptr<order::OrderGatewayClient> order_gw_;
    std::unique_ptr<vol::VolSurfaceClient> vol_client_;
    std::unique_ptr<order::OrderManager> order_mgr_;
    std::unique_ptr<strategy::IStrategy> strategy_;
    std::unique_ptr<backtest::BacktestClient> backtest_client_;

    bool refdata_ready_{false};
    bool surtr_ready_{false};
    bool strategy_started_{false};
    bool strategy_md_started_{false};

    // Bitmask of exchanges for which AccountSnapshot has been received.
    // Same bit layout as RefDataReady.exchangesLoaded (bit0=BINANCE, bit1=OKX, bit2=HYPERLIQUID).
    // Trading is gated until this matches configured_exchanges_mask when order_gw_ is active.
    uint8_t account_snapshot_ready_{0x00};
    bool account_snapshot_requests_sent_{false};

    // Service liveness watchdog.
    // Set to true when any service heartbeat goes stale; cleared on recovery.
    // MD and order-gateway callbacks are suppressed while paused.
    bool trading_paused_{false};
    uint64_t last_md_hb_recv_ns_{0};   // steady_clock receipt time of last Huginn heartbeat
    uint64_t last_gw_hb_recv_ns_{0};   // steady_clock receipt time of last Heimdall heartbeat
    uint64_t last_liveness_check_ns_{0};
};

}  // namespace fenrir
