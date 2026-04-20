#include "md_gateway/adapter/binance/binance_md_encoder.h"

namespace bpt::md_gateway::adapter::binance {

std::string build_streams_query(const SubscriptionMap& subs) {
    std::string streams;
    for (const auto& [id, entry] : subs.snapshot()) {
        if (!streams.empty())
            streams += '/';
        streams += entry.symbol + "@bookTicker/" + entry.symbol + "@aggTrade";
    }
    return streams;
}

}  // namespace bpt::md_gateway::adapter::binance
