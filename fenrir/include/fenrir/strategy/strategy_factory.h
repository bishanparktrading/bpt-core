#pragma once

#include "fenrir/config/config.h"
#include "fenrir/md/md_client.h"
#include "fenrir/order/order_manager.h"
#include "fenrir/refdata/refdata_client.h"
#include "fenrir/strategy/i_strategy.h"
#include "fenrir/vol/vol_surface_client.h"

#include <memory>

namespace fenrir::strategy {

class StrategyFactory {
public:
    // Creates and returns a strategy based on the AppConfig parameters.
    // md, order_mgr, and vol_client are optional (nullptr if not configured).
    // Throws std::invalid_argument if the strategy type is unknown.
    static std::unique_ptr<IStrategy> create(const config::FenrirConfig& cfg,
                                             refdata::RefdataClient& refdata,
                                             md::MdClient* md,
                                             order::OrderManager* order_mgr,
                                             vol::VolSurfaceClient* vol_client = nullptr);
};

}  // namespace fenrir::strategy
