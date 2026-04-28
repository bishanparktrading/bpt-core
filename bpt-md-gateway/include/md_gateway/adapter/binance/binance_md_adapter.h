#pragma once

/// \file
/// \brief Binance market-data adapter (book + trades + funding rate).

#include "md_gateway/adapter/binance/binance_funding_rate_stream.h"
#include "md_gateway/adapter/binance/binance_md_decoder.h"
#include "md_gateway/adapter/binance/binance_md_ws_client.h"
#include "md_gateway/adapter/common/adapter_base.h"

#include <atomic>

namespace bpt::md_gateway::adapter {

/// \brief Subscribes to Binance public WS, decodes frames, publishes SBE.
///
/// Two parallel streams:
///   - Main WS at stream.binance.com:9443 — subscriptions are baked
///     into the URL (`/stream?streams=<sym>@bookTicker/<sym>@aggTrade/...`),
///     so runtime subscribe / unsubscribe only take effect on the next
///     reconnect.
///   - Funding-rate WS (`fstream.binance.com/stream?streams=!markPrice@arr@1s`)
///     runs on its own thread inside BinanceFundingRateStream — Binance
///     hosts funding/mark on a separate global broadcast endpoint, so
///     it can't ride on the per-symbol main WS.
///
/// Uses Beast's WS-level control-frame pings (configured in
/// connect_and_subscribe) — the ws-client's ping_config is left at
/// nullopt; no application ping thread.
class BinanceMdAdapter : public AdapterBase {
public:
    explicit BinanceMdAdapter(const config::AdapterConfig& cfg, std::shared_ptr<messaging::IMdPublisher> md_pub);

    /// \brief Register a subscription. Lowercases the symbol — Binance stream names are lowercase.
    void subscribe(uint64_t instrument_id, std::string symbol, uint8_t depth = 0) override;

    /// \brief Start the main IO + publisher threads AND the funding-rate stream.
    void start() override;

    /// \brief Stop the main threads AND the funding-rate stream.
    void stop() override;

    [[nodiscard]] const char* exchange_name() const override { return "BINANCE"; }
    [[nodiscard]] bpt::common::util::LatencyHistogram& decode_latency_hist() noexcept override { return decoder_.decode_lat_; }

protected:
    /// \brief Open the main WS and bake subscriptions into the URL.
    /// \return nullptr if there are no subscriptions yet (run() retries after 100 ms).
    std::unique_ptr<bpt::common::ws::AnyWsStream> connect_and_subscribe() override;
    void read_loop(bpt::common::ws::AnyWsStream& ws) override;
    void parse_frame(std::string_view payload, uint64_t recv_ns) override;

private:
    BinanceMdDecoder decoder_;
    BinanceMdWsClient ws_client_;
    /// RunLoop::run signature needs a 'connected' atomic; AdapterBase
    /// already tracks connection state via on_connect/on_disconnect
    /// callbacks, so this flag is otherwise unused here.
    std::atomic<bool> rl_connected_{false};
    BinanceFundingRateStream fr_stream_;
};

}  // namespace bpt::md_gateway::adapter
