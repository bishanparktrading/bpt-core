#pragma once

#include "heimdall/adapter/common/i_order_adapter.h"

#include <boost/json.hpp>
#include <cstdint>
#include <functional>

namespace heimdall::adapter {

// Parses Hyperliquid WebSocket fill events into ExecEvents.
// Hyperliquid encodes the internal order_id directly as the cloid, so no
// mapping table is needed.
class HyperliquidExecParser {
public:
    std::function<void(const ExecEvent&)> on_exec_event;

    // Called when channel=="user" data.fills arrives.
    void handle_fills(const boost::json::array& fills, uint64_t recv_ns);
};

}  // namespace heimdall::adapter
