#include "fenrir/app/fenrir_app.h"
#include "fenrir/config/config.h"

#include <Aeron.h>
#include <execinfo.h>
#include <spdlog/spdlog.h>
#include <string>
#include <yggdrasil/aeron/aeron_utils.h>
#include <yggdrasil/logging.h>
#include <yggdrasil/signal.h>

int main(int argc, char* argv[]) {
    ygg::signal::install();

    const std::string config_path = (argc > 1) ? argv[1] : "config/fenrir.toml";

    fenrir::config::AppConfig app_cfg;
    try {
        app_cfg = fenrir::config::AppConfig::load(config_path);
    } catch (const std::exception& e) {
        ygg::logging::init("fenrir");
        spdlog::error("{}", e.what());
        return 1;
    }

    ygg::logging::init("fenrir", app_cfg.logging);

    ::aeron::Context aeron_ctx;
    if (!app_cfg.aeron.media_driver_dir.empty())
        aeron_ctx.aeronDir(app_cfg.aeron.media_driver_dir);
    aeron_ctx.errorHandler([](const std::exception& e) {
        spdlog::error("[Aeron] error handler: {}", e.what());
        void* frames[32];
        int n = ::backtrace(frames, 32);
        char** syms = ::backtrace_symbols(frames, n);
        for (int i = 0; i < n; ++i)
            spdlog::error("  {}", syms ? syms[i] : "???");
        free(syms);
        // do NOT exit — let the poll loop continue
    });
    auto aeron = ::aeron::Aeron::connect(aeron_ctx);
    spdlog::info("Connected to Aeron MediaDriver");

    try {
        fenrir::FenrirApp app(std::move(app_cfg), std::move(aeron));
        app.run();
    } catch (const std::exception& e) {
        spdlog::error("Fatal: {}", e.what());
        return 1;
    }

    return 0;
}
