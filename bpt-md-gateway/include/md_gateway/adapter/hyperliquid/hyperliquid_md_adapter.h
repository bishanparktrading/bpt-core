#pragma once

/// \file
/// \brief Hyperliquid market-data adapter.

#include "md_gateway/adapter/common/adapter_base.h"
#include "md_gateway/adapter/hyperliquid/hyperliquid_md_decoder.h"
#include "md_gateway/adapter/hyperliquid/hyperliquid_md_ws_client.h"

#include <atomic>

namespace bpt::md_gateway::adapter {

/// \brief Subscribes to Hyperliquid public WS, decodes frames, publishes SBE.
///
/// Connects to wss://api.hyperliquid.xyz/ws. Subscribes to l2Book,
/// trades, and activeAssetCtx per instrument. Runtime subscribe /
/// unsubscribe take effect immediately via the pending queue.
///
/// HL closes idle WebSockets ~60 s after the last client-sent message,
/// so the WS client's ping_config emits a JSON `{"method":"ping"}`
/// payload on a 20 s cadence (control-frame pings don't reset HL's
/// idle timer).
class HyperliquidMdAdapter : public AdapterBase {
public:
    explicit HyperliquidMdAdapter(const config::AdapterConfig& cfg, std::shared_ptr<messaging::IMdPublisher> md_pub);

    [[nodiscard]] const char* exchange_name() const override { return "HYPERLIQUID"; }
    [[nodiscard]] bpt::common::util::LatencyHistogram& decode_latency_hist() noexcept override { return decoder_.decode_lat_; }

    /// \brief Push the 3 subscribe frames (l2Book, trades, activeAssetCtx) immediately on connect.
    ///
    /// Bypasses on_tick — RunLoop's sync ws.read() doesn't honour
    /// expires_after in this Beast version, so on_tick can be starved
    /// while HL is streaming. Same pattern as OKX.
    void subscribe(uint64_t instrument_id, std::string symbol, uint8_t depth = 0) override;

protected:
    std::unique_ptr<bpt::common::ws::AnyWsStream> connect_and_subscribe() override;
    void read_loop(bpt::common::ws::AnyWsStream& ws) override;
    void parse_frame(std::string_view payload, uint64_t recv_ns) override;

private:
    HyperliquidMdDecoder decoder_;
    HyperliquidMdWsClient ws_client_;
    std::atomic<bool> rl_connected_{false};
};

}  // namespace bpt::md_gateway::adapter
