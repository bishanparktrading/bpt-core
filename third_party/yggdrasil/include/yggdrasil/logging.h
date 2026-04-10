#pragma once

// yggdrasil/logging.h — Shared logger config and initialisation for all services.
//
// Usage (in main, before any spdlog calls):
//   ygg::logging::init("fenrir");
//
//   ygg::logging::LogConfig cfg;
//   cfg.level = "debug";
//   cfg.flush_interval_ms = 1000;
//   ygg::logging::init("fenrir", cfg);
//
// To load config from a TOML table, include <yggdrasil/logging_toml.h> instead.

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ygg::logging {

struct LogConfig {
    std::string log_dir           = "logs";
    std::string level             = "info";   // trace/debug/info/warn/error/critical/off
    std::string flush_level       = "warn";   // flush-on threshold (>= this level triggers sync flush)
    bool        console           = true;
    bool        file              = true;
    uint32_t    async_queue_size  = 8192;     // ring buffer depth for the async logger
    uint32_t    async_threads     = 1;
    bool        block_on_overflow = false;    // false = discard_new (prefer for hot paths)
    uint32_t    max_file_size_mb  = 10;
    uint32_t    max_files         = 3;        // number of rotated files to retain
    std::string pattern;                      // empty = spdlog default pattern
    uint32_t    flush_interval_ms = 0;        // 0 = flush-on-level only; >0 also flushes periodically
};

inline spdlog::level::level_enum level_from_string(const std::string& s) {
    if (s == "trace")                    return spdlog::level::trace;
    if (s == "debug")                    return spdlog::level::debug;
    if (s == "info")                     return spdlog::level::info;
    if (s == "warn" || s == "warning")   return spdlog::level::warn;
    if (s == "error")                    return spdlog::level::err;
    if (s == "critical")                 return spdlog::level::critical;
    if (s == "off")                      return spdlog::level::off;
    return spdlog::level::info;
}

inline void init(const std::string& service_name, const LogConfig& cfg = {}) {
    spdlog::init_thread_pool(cfg.async_queue_size, cfg.async_threads);

    std::vector<spdlog::sink_ptr> sinks;

    if (cfg.console)
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    if (cfg.file) {
        std::filesystem::create_directories(cfg.log_dir);
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            cfg.log_dir + "/" + service_name + ".log",
            static_cast<std::size_t>(cfg.max_file_size_mb) * 1024 * 1024,
            cfg.max_files));
    }

    auto policy = cfg.block_on_overflow
        ? spdlog::async_overflow_policy::block
        : spdlog::async_overflow_policy::discard_new;

    auto logger = std::make_shared<spdlog::async_logger>(
        service_name, sinks.begin(), sinks.end(),
        spdlog::thread_pool(), policy);

    logger->set_level(level_from_string(cfg.level));
    logger->flush_on(level_from_string(cfg.flush_level));

    if (!cfg.pattern.empty())
        logger->set_pattern(cfg.pattern);

    if (cfg.flush_interval_ms > 0)
        spdlog::flush_every(std::chrono::milliseconds(cfg.flush_interval_ms));

    spdlog::set_default_logger(logger);
}

}  // namespace ygg::logging
