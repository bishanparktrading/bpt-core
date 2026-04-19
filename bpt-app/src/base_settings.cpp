#include "bpt_app/base_settings.h"

#include <bpt_common/logging_toml.h>

namespace bpt::app {

void load_base_settings(const toml::table& root, BaseSettings& base) {
    if (auto v = root["environment"].value<std::string>())
        base.environment = *v;

    if (const auto* a = root["aeron"].as_table()) {
        if (auto v = (*a)["media_driver_dir"].value<std::string>())
            base.media_driver_dir = *v;
    }

    if (const auto* l = root["logging"].as_table())
        base.logging = bpt::common::logging::from_toml(*l);

    if (const auto* m = root["metrics"].as_table()) {
        if (auto v = (*m)["port"].value<int64_t>())
            base.metrics_port = static_cast<uint16_t>(*v);
    }
}

}  // namespace bpt::app
