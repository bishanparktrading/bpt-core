#pragma once

/// \file
/// Port: pricer-side md-gateway control client. Publishes
/// MdSubscribeBatch with a stable correlation_id so md-gateway's
/// per-consumer refcounting can scope this consumer's desired set
/// independently of other consumers (strategy).

#include <cstdint>
#include <string>
#include <vector>

namespace bpt::pricer::md::api {

class MdSubscribeClient {
public:
    struct InstrumentDesc {
        uint64_t instrument_id;
        std::string exchange;  ///< canonical name, e.g. "DERIBIT"
        std::string symbol;    ///< venue-native, e.g. "BTC-15MAY26-65000-C"
        uint8_t depth{0};      ///< 0 = BBO only; >0 = top-N ladder
    };

    virtual ~MdSubscribeClient() = default;

    /// \brief Send the full desired option universe to md-gateway.
    ///
    /// `correlation_id` identifies this consumer to md-gateway's
    /// per-consumer refcounting. Pricer should reuse a stable id across
    /// its process lifetime so md-gateway treats successive calls as
    /// replace-the-same-consumer's-desired-set, not different consumers.
    virtual void publish(uint64_t correlation_id, const std::vector<InstrumentDesc>& instruments) = 0;
};

}  // namespace bpt::pricer::md::api
