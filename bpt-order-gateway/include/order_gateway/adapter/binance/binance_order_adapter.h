#pragma once

/// \file
/// \brief Binance order adapter — REST place + user-data WS for execs.

#include "order_gateway/adapter/binance/binance_exec_decoder.h"
#include "order_gateway/adapter/binance/binance_https_client.h"
#include "order_gateway/adapter/binance/binance_user_data_ws.h"
#include "order_gateway/adapter/common/credentials.h"
#include "order_gateway/adapter/common/order_adapter_base.h"

#include <string>

namespace bpt::order_gateway::adapter {

/// \brief Order adapter for Binance Spot.
///
/// Places orders via REST (POST /api/v3/order) and subscribes to the
/// user-data WebSocket (via listenKey) for exec reports. Transport is
/// split across four components:
///   - binance_https_client   — TLS REST client
///   - binance_auth           — query-string HMAC-SHA256 signer
///   - binance_action_encoder — pure query-param builders
///   - binance_user_data_ws   — listenKey lifecycle + read loop
///
/// The adapter itself is pure orchestration: ctor wiring, send_* methods
/// that compose action_encoder + auth + https_client, and a user-data
/// dispatch callback into BinanceExecDecoder.
///
/// Credentials are passed via ExchangeCredentials (api_key, secret_key).
class BinanceOrderAdapter : public OrderAdapterBase {
public:
    BinanceOrderAdapter(const config::AdapterConfig& cfg, const ExchangeCredentials& creds);

    [[nodiscard]] bpt::messages::ExchangeId::Value exchange_id() const override {
        return bpt::messages::ExchangeId::BINANCE;
    }
    [[nodiscard]] const char* exchange_name() const override { return "BINANCE"; }
    [[nodiscard]] bool is_connected() const override { return connected_.load(std::memory_order_relaxed); }

    AccountSnapshotData fetch_account_snapshot(uint64_t correlation_id) override;

protected:
    void connect_and_run() override;

    void do_send_new_order_blocking(const util::NewOrderRequest& req) override;
    void do_send_cancel_blocking(const util::CancelRequest& req) override;
    void do_send_cancel_all_blocking(const util::CancelAllRequest& req) override;
    void do_send_modify_blocking(const util::ModifyRequest& req) override;

private:
    void handle_user_data_message(const std::string& payload, uint64_t recv_ns);

    std::string api_key_;
    std::string secret_key_;

    BinanceExecDecoder decoder_;
    binance::BinanceHttpsClient https_client_;
    binance::BinanceUserDataWs user_data_ws_;
};

}  // namespace bpt::order_gateway::adapter
