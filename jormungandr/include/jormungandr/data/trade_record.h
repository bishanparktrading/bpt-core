#pragma once

#include <cstdint>
#include <string>

namespace jormungandr::data {

enum class TradeSide : int8_t { BUY = 0, SELL = 1 };

struct TradeRecord {
    uint64_t timestamp_ns;
    double price;
    double quantity;
    TradeSide side;
    std::string exchange;
    std::string symbol;
};

}  // namespace jormungandr::data
