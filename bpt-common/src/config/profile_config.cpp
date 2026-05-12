#include "bpt_common/config/profile_config.h"

#include "bpt_common/logging.h"

#include <fmt/format.h>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace bpt::common::config {

ProfileConfig load_profile_config(const std::string& path) {
    ProfileConfig out;
    toml::table root;
    try {
        root = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(fmt::format("profile_config: failed to parse '{}': {}", path, e.description()));
    }

    if (auto v = root["environment"].value<std::string>()) {
        out.environment = bpt::common::env_from_string(*v);
    } else {
        throw std::runtime_error(fmt::format("profile_config '{}' missing required `environment` field", path));
    }

    if (auto* arr = root["exchanges"].as_array()) {
        out.exchanges.reserve(arr->size());
        for (auto& elem : *arr) {
            if (auto v = elem.value<std::string>())
                out.exchanges.push_back(*v);
        }
    }

    if (auto v = root["exchange_config"].value<std::string>())
        out.exchange_config = *v;

    return out;
}

}  // namespace bpt::common::config
