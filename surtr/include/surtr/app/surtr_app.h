#pragma once

#include "surtr/config/settings.h"
#include "surtr/md/md_subscriber.h"
#include "surtr/messaging/status_publisher.h"
#include "surtr/messaging/vol_surface_publisher.h"
#include "surtr/refdata/refdata_subscriber.h"
#include "surtr/surface/surface_builder.h"

#include <bifrost_protocol/ExchangeId.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace surtr {

class SurtrApp {
public:
    SurtrApp(config::Settings settings, std::shared_ptr<aeron::Aeron> aeron);
    void run();

private:
    struct PerpInfo {
        std::string underlying;
        bifrost::protocol::ExchangeId::Value exchange_id;
    };

    config::Settings settings_;
    surface::SurfaceBuilder builder_;
    std::unique_ptr<messaging::VolSurfacePublisher> vol_pub_;
    std::unique_ptr<messaging::StatusPublisher> status_pub_;
    std::unique_ptr<md::MdSubscriber> md_sub_;
    std::unique_ptr<refdata::RefdataSubscriber> refdata_sub_;
    std::unordered_map<uint64_t, PerpInfo> perp_map_;
};

}  // namespace surtr
