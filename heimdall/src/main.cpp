// Heimdall — Order Gateway
// Odin's spear always hits its mark.

#include "heimdall/adapter/common/credentials.h"
#include "heimdall/app/heimdall_app.h"
#include "heimdall/config/settings.h"

#include <aws/core/Aws.h>
#include <map>
#include <string>
#include <sys/prctl.h>
#include <yggdrasil/aeron/aeron_utils.h>
#include <yggdrasil/logging.h>
#include <yggdrasil/secrets/secrets_client.h>
#include <yggdrasil/signal.h>

int main(int argc, char* argv[]) {
    // Suppress core dumps to protect key material (Hyperliquid private key)
    ::prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    ygg::signal::install();

    const std::string config_path = (argc > 1) ? argv[1] : "config/heimdall.toml";

    heimdall::config::Settings cfg;
    try {
        cfg = heimdall::config::load(config_path);
    } catch (const std::exception& e) {
        ygg::logging::init("heimdall");
        ygg::log::error("[Heimdall] Failed to load config: {}", e.what());
        return 1;
    }

    ygg::logging::init("heimdall", cfg.logging);
    ygg::log::info("[Heimdall] Starting — Odin's spear always hits its mark.");

    Aws::SDKOptions aws_options;
    Aws::InitAPI(aws_options);

    std::map<std::string, heimdall::adapter::ExchangeCredentials> creds;
    for (const auto& a_cfg : cfg.heimdall.adapters) {
        if (a_cfg.secret_name.empty()) {
            ygg::log::warn("[Heimdall] No secret_name for {} — adapter will have empty credentials", a_cfg.exchange);
            creds[a_cfg.exchange] = {};
            continue;
        }
        try {
            const auto kv = ygg::secrets::fetch(a_cfg.secret_name);
            creds[a_cfg.exchange] = heimdall::adapter::credentials_from_secret(a_cfg.exchange, kv);
            ygg::log::info("[Heimdall] Loaded credentials for {}", a_cfg.exchange);
        } catch (const std::exception& e) {
            ygg::log::error("[Heimdall] Failed to load credentials for {}: {}", a_cfg.exchange, e.what());
            Aws::ShutdownAPI(aws_options);
            return 1;
        }
    }

    std::shared_ptr<aeron::Aeron> aeron;
    try {
        aeron = ygg::aeron::connect(cfg.aeron.media_driver_dir);
        ygg::log::info("[Heimdall] Connected to Aeron MediaDriver");
    } catch (const std::exception& e) {
        ygg::log::error("[Heimdall] Failed to connect to Aeron: {}", e.what());
        Aws::ShutdownAPI(aws_options);
        return 1;
    }

    try {
        heimdall::HeimdallApp app(std::move(cfg), std::move(aeron), std::move(creds));
        app.run();
    } catch (const std::exception& e) {
        ygg::log::error("[Heimdall] Fatal: {}", e.what());
        Aws::ShutdownAPI(aws_options);
        return 1;
    }

    Aws::ShutdownAPI(aws_options);
    return 0;
}
