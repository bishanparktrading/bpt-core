#include "book/config/settings.h"

#include <bpt_app/base_settings.h>
#include <bpt_common/aeron/streams_map.h>
#include <bpt_common/logging.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <stdexcept>
#include <toml++/toml.hpp>
#include <unordered_set>

namespace bpt::book::config {

Settings load(const std::string& path) {
    toml::table root = toml::parse_file(path);

    // Match OG's pattern: shared exchange_config file holds per-adapter
    // endpoints + secret_name; the instance file narrows via an
    // `exchanges = [...]` filter.
    std::string exchange_config_path;
    toml::array adapters_arr;
    if (auto v = root["exchange_config"].value<std::string>()) {
        exchange_config_path = *v;
        toml::table exchange = toml::parse_file(*v);
        if (auto* arr = exchange["adapters"].as_array())
            adapters_arr = *arr;
    } else if (auto* arr = root["adapters"].as_array()) {
        adapters_arr = *arr;
    }

    Settings s;
    bpt::app::load_base_settings(root, s.base);

    bpt::common::log::info("Environment: {}", bpt::app::to_string(s.base.environment));

    std::unordered_set<std::string> exchange_filter;
    if (auto* arr = root["exchanges"].as_array()) {
        for (auto& elem : *arr)
            if (auto v = elem.value<std::string>())
                exchange_filter.insert(*v);
        s.exchanges = {exchange_filter.begin(), exchange_filter.end()};
        bpt::common::log::info("Exchange filter: [{}]", fmt::join(s.exchanges, ", "));
    }

    bpt::common::config::AeronStreamMap shared_streams;
    if (auto v = root["aeron_config"].value<std::string>()) {
        shared_streams = bpt::common::config::load_shared_streams(*v);
        bpt::common::log::info("Loaded shared aeron stream map from {} ({} streams)",
                               *v, shared_streams.stream_ids.size());
        if (!shared_streams.media_driver_dir.empty())
            s.base.media_driver_dir = shared_streams.media_driver_dir;
    }

    using bpt::common::config::resolve_stream;
    s.aeron.balance_snapshot = resolve_stream(shared_streams, "balance_snapshot", 6001);

    if (auto* b = root["book"].as_table()) {
        if (auto v = (*b)["poll_interval_ms"].value<int64_t>())
            s.book.poll_interval_ms = static_cast<uint32_t>(*v);
    }

    for (auto& elem : adapters_arr) {
        auto* a = elem.as_table();
        if (!a)
            continue;

        auto exchange_name = (*a)["exchange"].value<std::string>().value_or("");
        if (!exchange_filter.count(exchange_name))
            continue;

        AdapterConfig ac;
        ac.exchange = exchange_name;
        if (auto v = (*a)["secret_name"].value<std::string>())
            ac.secret_name = *v;
        if (auto v = (*a)["testnet"].value<bool>())
            ac.testnet = *v;
        if (auto v = (*a)["rest_host"].value<std::string>())
            ac.rest_host = *v;
        if (auto v = (*a)["rest_port"].value<std::string>())
            ac.rest_port = *v;
        if (auto v = (*a)["wallet_address"].value<std::string>())
            ac.wallet_address = *v;

        if (ac.rest_host.empty())
            throw std::runtime_error(fmt::format("Adapter {} missing required rest_host", exchange_name));

        // Fatal: prod environment with testnet adapters. Mismatch would
        // make bpt-book publish stale / wrong numbers into the prod
        // message bus — dashboards would lie.
        if (s.base.is_prod() && ac.testnet)
            throw std::runtime_error(
                fmt::format("environment = \"prod\" but adapter {} has testnet = true — refusing to start",
                            exchange_name));

        s.book.adapters.push_back(std::move(ac));
    }

    return s;
}

}  // namespace bpt::book::config
