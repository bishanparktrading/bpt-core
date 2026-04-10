#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "heimdall/adapter/binance/binance_exec_parser.h"
#include "heimdall/adapter/common/credentials.h"
#include "heimdall/adapter/common/order_adapter_base.h"

namespace heimdall::adapter {

// BinanceOrderAdapter connects to the Binance user data stream (via listenKey)
// for execution reports and places/cancels orders via REST (POST
// /api/v3/order).
//
// Credentials are passed directly via ExchangeCredentials (api_key, secret_key).
//
// listenKey lifecycle:
//   On connect: POST /api/v3/userDataStream to create listenKey.
//   Every 30 minutes: PUT /api/v3/userDataStream to extend.
//   On disconnect: DELETE /api/v3/userDataStream.
class BinanceOrderAdapter : public OrderAdapterBase {
public:
    BinanceOrderAdapter(const config::AdapterConfig& cfg, const ExchangeCredentials& creds);

    void send_new_order(const bifrost::protocol::NewOrder& order) override;
    void send_cancel(const bifrost::protocol::CancelOrder& cancel,
                     const std::string& native_symbol) override;
    void send_cancel_all(uint64_t instrument_id) override;
    void send_modify(const bifrost::protocol::ModifyOrder& modify,
                     const std::string& native_symbol) override;

    [[nodiscard]] bifrost::protocol::ExchangeId::Value exchange_id() const override {
        return bifrost::protocol::ExchangeId::BINANCE;
    }
    [[nodiscard]] const char* exchange_name() const override { return "BINANCE"; }
    [[nodiscard]] bool is_connected() const override {
        return connected_.load(std::memory_order_relaxed);
    }

    AccountSnapshotData fetch_account_snapshot(uint64_t correlation_id) override;

protected:
    void connect_and_run() override;

private:
    // HTTPS helper: synchronous POST/PUT/DELETE to Binance REST API.
    std::string https_request(const std::string& method, const std::string& path,
                              const std::string& body, bool with_api_key);

    // Build a signed query string for private REST endpoints.
    std::string sign_query(const std::string& params) const;

    // listenKey management.
    std::string create_listen_key();
    void extend_listen_key(const std::string& listen_key);
    void delete_listen_key(const std::string& listen_key);

    // Route a raw userDataStream WebSocket event to the parser.
    void handle_user_data_message(const std::string& payload, uint64_t recv_ns);

    std::string api_key_;
    std::string secret_key_;

    // Track last listenKey ping time
    std::chrono::steady_clock::time_point last_ping_;
    std::string listen_key_;

    BinanceExecParser parser_;
};

}  // namespace heimdall::adapter
