// bridge — Aeron → WebSocket forwarder for the dashboard.

#include "bridge/bridge_app.h"
#include "bridge/settings.h"

#include <CLI/CLI.hpp>
#include <memory>
#include <string>
#include <bpt_app/app.h>
#include <bpt_common/logging.h>

int main(int argc, char** argv) {
    CLI::App cli{"bpt-bridge — Aeron → WebSocket forwarder for dashboard"};
    std::string config_path = "config/bridge.toml";
    std::string strategy_override;
    std::string symbol_override;
    std::string exchange_override;
    std::string mode_override;
    std::string instrument_type_override;
    uint64_t    instrument_id_override = 0;
    cli.add_option("-c,--config", config_path, "Path to TOML config file")
        ->capture_default_str()
        ->check(CLI::ExistingFile);
    cli.add_option("--strategy-name", strategy_override, "Override session.strategy");
    cli.add_option("--symbol", symbol_override, "Override session.symbol");
    cli.add_option("--exchange", exchange_override, "Override session.exchange");
    cli.add_option("--mode", mode_override, "Override session.mode (paper|live|mock)");
    cli.add_option("--instrument-type", instrument_type_override,
                   "Override session.instrument_type (SPOT|PERP|FUTURE|OPTION)");
    cli.add_option("--instrument-id", instrument_id_override, "Override session.instrument_id");
    CLI11_PARSE(cli, argc, argv);

    bridge::config::Settings settings;
    try {
        settings = bridge::config::load(config_path);
    } catch (const std::exception& e) {
        bpt::common::logging::init("bridge");
        bpt::common::log::error("Failed to load config: {}", e.what());
        return 1;
    }

    // CLI overrides take precedence over TOML values.
    if (!strategy_override.empty())        settings.strategy         = strategy_override;
    if (!symbol_override.empty())          settings.symbol           = symbol_override;
    if (!exchange_override.empty())        settings.exchange         = exchange_override;
    if (!mode_override.empty())            settings.mode             = mode_override;
    if (!instrument_type_override.empty()) settings.instrument_type  = instrument_type_override;
    if (instrument_id_override > 0)        settings.instrument_id    = instrument_id_override;

    try {
        return bpt::app::run("bridge", std::move(settings),
            [](auto& cfg, auto& ctx) -> std::unique_ptr<bpt::app::IService> {
                return std::make_unique<bridge::BridgeApp>(std::move(cfg), ctx.aeron);
            });
    } catch (const std::exception& e) {
        bpt::common::log::error("Fatal: {}", e.what());
        return 1;
    }
}
