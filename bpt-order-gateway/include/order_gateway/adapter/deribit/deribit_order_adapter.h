#pragma once

/// \file
/// \brief Deribit order adapter — JSON-RPC 2.0 over single private WS.

#include "order_gateway/adapter/common/credentials.h"
#include "order_gateway/adapter/common/order_adapter_base.h"
#include "order_gateway/adapter/deribit/deribit_exec_decoder.h"
#include "order_gateway/adapter/deribit/deribit_ws_client.h"

#include <atomic>
#include <boost/json.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bpt::order_gateway::adapter {

/// \brief Order adapter for Deribit (private WS, JSON-RPC 2.0 for order ops + execs).
///
/// Endpoint: wss://www.deribit.com/ws/api/v2 (prod) or
/// wss://test.deribit.com/ws/api/v2 (testnet).
/// Authentication: JSON-RPC `public/auth` with client_credentials grant.
/// Order ops: `private/buy`, `private/sell`. Exec reports flow on the
/// `user.orders.any.raw` subscription channel. Heartbeat is set up via
/// `public/set_heartbeat` and we respond to incoming test_request with
/// `public/test` — Deribit drops the session in 30 s otherwise.
///
/// Transport is split across:
///   - deribit_action_encoder — pure JSON-RPC envelope builders
///   - deribit_ws_client      — WS lifecycle + thread-safe send
///
/// Credentials via ExchangeCredentials (client_id, client_secret).
class DeribitOrderAdapter : public OrderAdapterBase {
public:
    DeribitOrderAdapter(const config::AdapterConfig& cfg, const ExchangeCredentials& creds);

    [[nodiscard]] bpt::messages::ExchangeId::Value exchange_id() const override {
        return bpt::messages::ExchangeId::DERIBIT;
    }
    [[nodiscard]] const char* exchange_name() const override { return "DERIBIT"; }
    [[nodiscard]] bool is_connected() const override { return connected_.load(std::memory_order_relaxed); }

    AccountSnapshotData fetch_account_snapshot(uint64_t correlation_id) override;

protected:
    void connect_and_run() override;

    void do_send_new_order_blocking(const util::NewOrderRequest& req) override;
    void do_send_cancel_blocking(const util::CancelRequest& req) override;
    void do_send_cancel_all_blocking(const util::CancelAllRequest& req) override;
    void do_send_modify_blocking(const util::ModifyRequest& req) override;

private:
    void handle_message(const std::string& payload, uint64_t recv_ns);

    std::string client_id_;
    std::string client_secret_;

    std::atomic<bool> logged_in_{false};

    /// 8-char hex prefix unique to this process start — prevents label
    /// collisions with orders from previous sessions still live on Deribit.
    std::string session_prefix_;

    std::atomic<uint64_t> jsonrpc_id_{1};  ///< monotonic JSON-RPC request id

    /// Orders queued while waiting for the auth response; flushed by
    /// handle_message when the auth reply is seen.
    mutable std::mutex pending_mu_;
    std::vector<std::string> pending_sends_;

    /// Promise/future bridge for synchronous JSON-RPC calls. The caller
    /// (fetch_account_snapshot, on its own thread) registers a promise
    /// keyed by the JSON-RPC request id, sends the frame, and waits on
    /// the future with a timeout. handle_message resolves the promise
    /// when the matching id-keyed response arrives. Used today only for
    /// `private/get_account_summaries` + `private/get_positions`; order
    /// flow stays event-driven via DeribitExecDecoder.
    mutable std::mutex pending_responses_mu_;
    std::unordered_map<uint64_t, std::shared_ptr<std::promise<boost::json::value>>> pending_responses_;

    /// Send a JSON-RPC request and block (caller's thread) until the
    /// response arrives or the timeout fires. Returns the `result` value
    /// from the response. Throws std::runtime_error on timeout / WS send
    /// failure / Deribit error envelope.
    boost::json::value send_and_wait(const std::string& method,
                                     const std::string& params_json,
                                     std::chrono::milliseconds timeout);

    DeribitExecDecoder decoder_;
    deribit::DeribitWsClient ws_client_;
};

}  // namespace bpt::order_gateway::adapter
