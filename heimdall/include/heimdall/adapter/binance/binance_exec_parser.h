#pragma once

#include "heimdall/adapter/common/i_order_adapter.h"

#include <boost/json.hpp>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace heimdall::adapter {

// Parses Binance executionReport WebSocket events into ExecEvents.
// Owns the cloid→order_id map so both the WS handler and send_new_order
// can register/look up client order IDs without locking from two places.
class BinanceExecParser {
public:
    std::function<void(const ExecEvent&)> on_exec_event;

    // Register a client order ID before the order is sent.
    void register_order(const std::string& cloid, uint64_t order_id);

    // Called for each "executionReport" event on the user-data WebSocket.
    void handle_execution_report(const boost::json::object& obj, uint64_t recv_ns);

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, uint64_t> cloid_to_order_id_;
};

}  // namespace heimdall::adapter
