// Jormungandr — Backtest Exchange Simulator
// The world serpent that swallows the entire market history.

#include "jormungandr/app/jormungandr_app.h"
#include "jormungandr/config/settings.h"

#include <string>
#include <yggdrasil/logging.h>
#include <yggdrasil/signal.h>

int main(int argc, char* argv[]) {
    ygg::signal::install();

    const std::string config_path = (argc > 1) ? argv[1] : "config/jormungandr.toml";

    jormungandr::config::Settings settings;
    try {
        settings = jormungandr::config::load(config_path);
    } catch (const std::exception& e) {
        ygg::logging::init("jormungandr");
        ygg::log::error("[Jormungandr] Failed to load config: {}", e.what());
        return 1;
    }

    ygg::logging::init("jormungandr", settings.logging);
    ygg::log::info("[Jormungandr] Starting — the world serpent awakens.");

    try {
        jormungandr::JormungandrApp app(std::move(settings));
        app.run();
    } catch (const std::exception& e) {
        ygg::log::error("[Jormungandr] Fatal: {}", e.what());
        return 1;
    }

    return 0;
}
