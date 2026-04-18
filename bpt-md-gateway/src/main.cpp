#include "md_gateway/app/md_gateway_app.h"
#include "md_gateway/config/settings.h"

#include <CLI/CLI.hpp>
#include <chrono>
#include <string>
#include <yggdrasil/aeron/aeron_utils.h>
#include <yggdrasil/logging.h>
#include <yggdrasil/signal.h>
#include <yggdrasil/util/tsc_clock.h>

int main(int argc, char* argv[]) {
    ygg::signal::install();

    CLI::App app{"bpt-md-gateway — market data aggregator"};
    std::string config_path = "config/bpt-md-gateway.toml";
    app.add_option("-c,--config", config_path, "Path to TOML config file")
        ->capture_default_str()
        ->check(CLI::ExistingFile);
    CLI11_PARSE(app, argc, argv);

    bpt::md_gateway::config::Settings cfg;
    try {
        cfg = bpt::md_gateway::config::load(config_path);
    } catch (const std::exception& e) {
        ygg::logging::init("bpt-md-gateway");
        ygg::log::error("Failed to load config: {}", e.what());
        return 1;
    }

    ygg::logging::init("bpt-md-gateway", cfg.logging);
    ygg::util::TscClock::calibrate();

    auto aeron = ygg::aeron::connect(cfg.aeron.media_driver_dir);
    ygg::log::info("MdGateway connected to Aeron MediaDriver");

    try {
        bpt::md_gateway::MdGatewayApp app(std::move(cfg), std::move(aeron));
        app.run();
    } catch (const std::exception& e) {
        ygg::log::error("Fatal: {}", e.what());
        return 1;
    }

    return 0;
}
