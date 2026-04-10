#include "muninn/adapter/credentials.h"
#include "muninn/app/muninn_app.h"
#include "muninn/config/settings.h"

#include <aws/core/Aws.h>
#include <chrono>
#include <map>
#include <spdlog/spdlog.h>
#include <string>
#include <yggdrasil/aeron/aeron_utils.h>
#include <yggdrasil/logging.h>
#include <yggdrasil/secrets/secrets_client.h>
#include <yggdrasil/signal.h>
#include <yggdrasil/util/tsc_clock.h>

int main(int argc, char** argv) {
    ygg::signal::install();

    std::string config_path = "config/muninn.toml";
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--config")
            config_path = argv[i + 1];
    }

    muninn::config::Settings settings;
    try {
        settings = muninn::config::load(config_path);
    } catch (const std::exception& e) {
        ygg::logging::init("muninn");
        spdlog::error("Failed to load config: {}", e.what());
        return 1;
    }

    ygg::logging::init("muninn", settings.logging);
    ygg::util::TscClock::calibrate();
    spdlog::info("Starting Muninn Reference Data Service...");

    Aws::SDKOptions aws_options;
    Aws::InitAPI(aws_options);

    std::map<std::string, muninn::adapter::ExchangeCredentials> creds;
    for (const auto& a_cfg : settings.adapters) {
        if (a_cfg.secret_name.empty()) {
            spdlog::warn("[Muninn] No secret_name for {} — adapter will have empty credentials", a_cfg.exchange);
            creds[a_cfg.exchange] = {};
            continue;
        }
        try {
            const auto kv = ygg::secrets::fetch(a_cfg.secret_name);
            creds[a_cfg.exchange] = muninn::adapter::credentials_from_secret(a_cfg.exchange, kv);
            spdlog::info("[Muninn] Loaded credentials for {}", a_cfg.exchange);
        } catch (const std::exception& e) {
            spdlog::error("[Muninn] Failed to load credentials for {}: {}", a_cfg.exchange, e.what());
            Aws::ShutdownAPI(aws_options);
            return 1;
        }
    }

    auto aeron = ygg::aeron::connect(settings.media_driver_dir);

    int rc = 0;
    try {
        muninn::MuninnApp app(std::move(settings), std::move(aeron), std::move(creds));
        app.run();
    } catch (const std::exception& e) {
        spdlog::error("Fatal: {}", e.what());
        rc = 1;
    }

    Aws::ShutdownAPI(aws_options);
    return rc;
}
