#include <spdlog/spdlog.h>

#include <toml++/toml.hpp>

#include "jormungandr/config/settings.h"

namespace jormungandr::config {

namespace {

ygg::config::StreamConfig load_stream(const toml::table* t, std::string default_channel,
                                      int32_t default_stream_id) {
    ygg::config::StreamConfig s{std::move(default_channel), default_stream_id};
    if (!t) return s;
    if (auto v = (*t)["channel"].value<std::string>()) s.channel = *v;
    if (auto v = (*t)["stream_id"].value<int64_t>()) s.stream_id = static_cast<int32_t>(*v);
    return s;
}

}  // namespace

Settings load(const std::string& path) {
    spdlog::info("[Jormungandr] Loading config from: {}", path);
    toml::table root = toml::parse_file(path);

    Settings s;

    if (auto* aeron = root["aeron"].as_table()) {
        if (auto v = (*aeron)["media_driver_dir"].value<std::string>())
            s.aeron.media_driver_dir = *v;
        s.aeron.backtest_ack = load_stream((*aeron)["backtest_ack"].as_table(), "aeron:ipc", 9001);
        s.aeron.backtest_control =
            load_stream((*aeron)["backtest_control"].as_table(), "aeron:ipc", 9002);
    }

    if (auto* sim = root["simulation"].as_table()) {
        if (auto v = (*sim)["start"].value<std::string>()) s.simulation.start = *v;
        if (auto v = (*sim)["end"].value<std::string>()) s.simulation.end = *v;
        if (auto v = (*sim)["allow_partial_data"].value<bool>())
            s.simulation.allow_partial_data = *v;

        if (auto* lat = (*sim)["latency"].as_table()) {
            if (auto v = (*lat)["cex_base_ms"].value<int64_t>())
                s.simulation.latency.cex_base_ms = static_cast<uint32_t>(*v);
            if (auto v = (*lat)["hyperliquid_base_ms"].value<int64_t>())
                s.simulation.latency.hyperliquid_base_ms = static_cast<uint32_t>(*v);
            if (auto v = (*lat)["hyperliquid_jitter_ms"].value<int64_t>())
                s.simulation.latency.hyperliquid_jitter_ms = static_cast<uint32_t>(*v);
        }
    }

    if (auto* d = root["data"].as_table())
        if (auto v = (*d)["local_cache"].value<std::string>()) s.data.local_cache = *v;

    if (auto* ep = root["endpoints"].as_table()) {
        if (auto v = (*ep)["binance_md_port"].value<int64_t>())
            s.endpoints.binance_md_port = static_cast<uint16_t>(*v);
        if (auto v = (*ep)["okx_md_port"].value<int64_t>())
            s.endpoints.okx_md_port = static_cast<uint16_t>(*v);
        if (auto v = (*ep)["hyperliquid_md_port"].value<int64_t>())
            s.endpoints.hyperliquid_md_port = static_cast<uint16_t>(*v);
        if (auto v = (*ep)["deribit_md_port"].value<int64_t>())
            s.endpoints.deribit_md_port = static_cast<uint16_t>(*v);
        if (auto v = (*ep)["binance_order_port"].value<int64_t>())
            s.endpoints.binance_order_port = static_cast<uint16_t>(*v);
        if (auto v = (*ep)["okx_order_port"].value<int64_t>())
            s.endpoints.okx_order_port = static_cast<uint16_t>(*v);
        if (auto v = (*ep)["hyperliquid_order_port"].value<int64_t>())
            s.endpoints.hyperliquid_order_port = static_cast<uint16_t>(*v);
        if (auto v = (*ep)["deribit_order_port"].value<int64_t>())
            s.endpoints.deribit_order_port = static_cast<uint16_t>(*v);
    }

    if (auto* arr = root["instruments"].as_array()) {
        for (auto& elem : *arr) {
            auto* t = elem.as_table();
            if (!t) continue;
            InstrumentConfig ic;
            if (auto v = (*t)["exchange"].value<std::string>()) ic.exchange = *v;
            if (auto v = (*t)["symbol"].value<std::string>()) ic.symbol = *v;
            if (!ic.exchange.empty() && !ic.symbol.empty()) s.instruments.push_back(std::move(ic));
        }
    }

    if (auto* l = root["logging"].as_table()) {
        if (auto v = (*l)["level"].value<std::string>()) s.logging.level = *v;
        if (auto v = (*l)["dir"].value<std::string>()) s.logging.dir = *v;
    }

    if (auto* r = root["results"].as_table()) {
        if (auto v = (*r)["output_dir"].value<std::string>()) s.results.output_dir = *v;
        if (auto v = (*r)["starting_capital"].value<double>()) s.results.starting_capital = *v;
    }

    if (auto* m = root["metrics"].as_table())
        if (auto v = (*m)["port"].value<int64_t>()) s.metrics_port = static_cast<uint16_t>(*v);

    spdlog::info("[Jormungandr] Backtest window: {} → {}", s.simulation.start, s.simulation.end);
    spdlog::info("[Jormungandr] Instruments: {}", s.instruments.size());

    return s;
}

}  // namespace jormungandr::config
