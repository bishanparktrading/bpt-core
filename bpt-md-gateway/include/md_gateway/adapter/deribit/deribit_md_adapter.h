#pragma once

/// \file
/// \brief Deribit market-data adapter (JSON-RPC 2.0 over WS).

#include "md_gateway/adapter/common/adapter_base.h"
#include "md_gateway/adapter/deribit/deribit_md_decoder.h"
#include "md_gateway/adapter/deribit/deribit_md_ws_client.h"

#include <atomic>
#include <cstdint>

namespace bpt::md_gateway::adapter {

/// \brief Subscribes to Deribit public WS, decodes frames, publishes SBE.
///
/// Connects to wss://www.deribit.com/ws/api/v2 using JSON-RPC 2.0. Sends
/// public/set_heartbeat on connect; responds to incoming test_request
/// with public/test — Deribit disconnects within 30 s if this is missed.
/// Detects order-book gaps via prev_change_id; affected instruments are
/// automatically resubscribed.
///
/// Deribit's set_heartbeat keeps the session alive, so the WS client's
/// ping_config is left at nullopt — no application ping thread.
class DeribitMdAdapter : public AdapterBase {
public:
    explicit DeribitMdAdapter(const config::AdapterConfig& cfg, std::shared_ptr<messaging::IMdPublisher> md_pub);

    /// \brief Unsubscribe + clear gap-detection state for the instrument in the decoder.
    void unsubscribe(uint64_t instrument_id) override;

    /// \brief Push subscribe frames immediately on connect (bypassing on_tick).
    ///
    /// Same reason as the OKX / HL adapters: RunLoop's sync ws.read()
    /// doesn't honour expires_after in this Beast version, so on_tick
    /// can be starved while the venue is streaming.
    void subscribe(uint64_t instrument_id, std::string symbol, uint8_t depth = 0) override;

    [[nodiscard]] const char* exchange_name() const override { return "DERIBIT"; }
    [[nodiscard]] bpt::common::util::LatencyHistogram& decode_latency_hist() noexcept override { return decoder_.decode_lat_; }

protected:
    /// \brief 2 s back-off — Deribit is slower to recover than the CEXs.
    std::chrono::milliseconds reconnect_delay() const override;

    std::unique_ptr<bpt::common::ws::AnyWsStream> connect_and_subscribe() override;
    void read_loop(bpt::common::ws::AnyWsStream& ws) override;
    void parse_frame(std::string_view payload, uint64_t recv_ns) override;

private:
    DeribitMdDecoder decoder_;
    DeribitMdWsClient ws_client_;
    std::atomic<bool> rl_connected_{false};
};

}  // namespace bpt::md_gateway::adapter
