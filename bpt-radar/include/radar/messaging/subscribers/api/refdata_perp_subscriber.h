#pragma once

/// \file
/// Port: refdata-snapshot consumer scoped to perpetual instruments only.
/// Aeron concrete in `aeron/refdata_perp_subscriber.h`.

#include <cstdint>
#include <functional>
#include <string>

namespace bpt::radar::messaging::api {

class RefdataPerpSubscriber {
public:
    struct PerpInfo {
        uint64_t instrument_id;
        std::string underlying;  ///< canonical, e.g. "BTC"
        uint8_t exchange_id;     ///< bpt::messages::ExchangeId::Value
    };

    using OnPerpFn = std::function<void(const PerpInfo&)>;

    virtual ~RefdataPerpSubscriber() = default;

    virtual int poll(int fragment_limit = 4) = 0;

    OnPerpFn on_perp;
};

}  // namespace bpt::radar::messaging::api
