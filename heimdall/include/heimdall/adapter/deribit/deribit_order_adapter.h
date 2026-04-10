#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "heimdall/adapter/common/credentials.h"
#include "heimdall/adapter/common/order_adapter_base.h"
#include "heimdall/adapter/deribit/deribit_exec_parser.h"

namespace heimdall::adapter {

// DeribitOrderAdapter connects to Deribit private WebSocket for order flow.
//
// WS endpoint: wss://www.deribit.com/ws/api/v2 (prod)
//              wss://test.deribit.com/ws/api/v2 (testnet)
// Authentication: JSON-RPC public/auth with client_credentials grant.
// Order placement: private/buy, private/sell via WebSocket JSON-RPC.
// Execution reports: user.orders.any.raw subscription channel.
// Heartbeat: public/set_heartbeat + respond to test_request with public/test.
//
// Credentials are passed directly via ExchangeCredentials (client_id, client_secret).
class DeribitOrderAdapter : public OrderAdapterBase {
public:
    DeribitOrderAdapter(const config::AdapterConfig& cfg, const ExchangeCredentials& creds);

    void send_new_order(const bifrost::protocol::NewOrder& order) override;
    void send_cancel(const bifrost::protocol::CancelOrder& cancel,
                     const std::string& native_symbol) override;
    void send_cancel_all(uint64_t instrument_id) override;
    void send_modify(const bifrost::protocol::ModifyOrder& modify,
                     const std::string& native_symbol) override;

    [[nodiscard]] bifrost::protocol::ExchangeId::Value exchange_id() const override {
        return bifrost::protocol::ExchangeId::DERIBIT;
    }
    [[nodiscard]] const char* exchange_name() const override { return "DERIBIT"; }
    [[nodiscard]] bool is_connected() const override {
        return connected_.load(std::memory_order_relaxed);
    }

    AccountSnapshotData fetch_account_snapshot(uint64_t correlation_id) override;

protected:
    void connect_and_run() override;

private:
    void handle_message(const std::string& payload, uint64_t recv_ns);

    // Build JSON-RPC auth message. Increments jsonrpc_id_.
    std::string build_auth_msg();

    std::string client_id_;
    std::string client_secret_;

    std::atomic<bool> logged_in_{false};

    // 8-char hex prefix unique to this process start — prevents label collisions
    // with orders from previous sessions still live on Deribit.
    std::string session_prefix_;

    // JSON-RPC request id counter — used by adapter for auth, heartbeat, send_*.
    std::atomic<uint64_t> jsonrpc_id_{1};

    // Outbound message queue for orders placed before auth completes.
    // ws_send_ is nulled on disconnect and guarded by send_mu_.
    mutable std::mutex send_mu_;
    std::vector<std::string> pending_sends_;
    std::function<void(const std::string&)> ws_send_;

    DeribitExecParser parser_;
};

}  // namespace heimdall::adapter
