#pragma once

// yggdrasil/logging.h — Shared logger initialisation for all Fenrir services.
//
// Usage (in main, before any spdlog calls):
//   ygg::logging::init("fenrir");
//   ygg::logging::init("heimdall", "logs", spdlog::level::debug);

#include <spdlog/async.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>

namespace ygg::logging {

// Convert a log level string to the spdlog enum.  Returns info on unknown input.
inline spdlog::level::level_enum level_from_string(const std::string& s) {
    if (s == "trace")
        return spdlog::level::trace;
    if (s == "debug")
        return spdlog::level::debug;
    if (s == "info")
        return spdlog::level::info;
    if (s == "warn" || s == "warning")
        return spdlog::level::warn;
    if (s == "error")
        return spdlog::level::err;
    if (s == "critical")
        return spdlog::level::critical;
    if (s == "off")
        return spdlog::level::off;
    return spdlog::level::info;
}

// Initialise an async spdlog logger with a rotating file sink and a colour
// console sink.  Sets it as the spdlog default logger.
//
// - service_name : used as the logger name and as the log filename stem
//                  (e.g. "fenrir" → logs/fenrir.log)
// - log_dir      : directory for the rotating file (default "logs")
// - level        : minimum log level (default info)
inline void init(const std::string& service_name,
                 const std::string& log_dir = "logs",
                 spdlog::level::level_enum level = spdlog::level::info) {
    spdlog::init_thread_pool(8192, 1);

    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_dir + "/" + service_name + ".log",
                                                                       10 * 1024 * 1024,  // 10 MB per file
                                                                       3                  // keep 3 rotated files
    );

    auto logger = std::make_shared<spdlog::async_logger>(service_name,
                                                         spdlog::sinks_init_list{console, file},
                                                         spdlog::thread_pool(),
                                                         spdlog::async_overflow_policy::discard_new);

    logger->set_level(level);
    logger->flush_on(level);
    spdlog::set_default_logger(logger);
}

}  // namespace ygg::logging
