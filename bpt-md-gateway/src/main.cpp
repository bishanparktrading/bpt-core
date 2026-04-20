#include "md_gateway/app/md_gateway_app.h"
#include "md_gateway/config/settings.h"

#include <CLI/CLI.hpp>
#include <memory>
#include <string>
#include <bpt_app/app.h>
#include <bpt_common/logging.h>

int main(int argc, char* argv[]) {
    CLI::App cli{"bpt-md-gateway — market data aggregator"};
    std::string config_path;
    cli.add_option("-c,--config", config_path, "Path to TOML config file")
        ->required()
        ->check(CLI::ExistingFile);
    CLI11_PARSE(cli, argc, argv);

    bpt::md_gateway::config::Settings cfg;
    try {
        cfg = bpt::md_gateway::config::load(config_path);
    } catch (const std::exception& e) {
        bpt::common::logging::init("bpt-md-gateway");
        bpt::common::log::error("Failed to load config: {}", e.what());
        return 1;
    }

    try {
        return bpt::app::run("bpt-md-gateway", std::move(cfg),
            [](auto& settings, auto& ctx) -> std::unique_ptr<bpt::app::IService> {
                return std::make_unique<bpt::md_gateway::MdGatewayApp>(
                    std::move(settings), ctx.aeron);
            });
    } catch (const std::exception& e) {
        bpt::common::log::error("Fatal: {}", e.what());
        return 1;
    }
}
