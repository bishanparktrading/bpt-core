#include "md_gateway/adapter/okx/okx_md_encoder.h"

#include <fmt/format.h>

namespace bpt::md_gateway::adapter::okx {

std::string build_subscribe_payload(const std::string& symbol, uint8_t depth) {
    const char* book_channel = (depth == 0) ? "bbo-tbt" : (depth <= 5) ? "books5" : "books";

    const bool is_swap = symbol.size() > 5 && symbol.substr(symbol.size() - 5) == "-SWAP";
    if (is_swap) {
        return fmt::format(
            R"({{"op":"subscribe","args":[)"
            R"({{"channel":"{}","instId":"{}"}},)"
            R"({{"channel":"trades","instId":"{}"}},)"
            R"({{"channel":"funding-rate","instId":"{}"}}]}})",
            book_channel, symbol, symbol, symbol);
    }
    return fmt::format(
        R"({{"op":"subscribe","args":[)"
        R"({{"channel":"{}","instId":"{}"}},)"
        R"({{"channel":"trades","instId":"{}"}}]}})",
        book_channel, symbol, symbol);
}

}  // namespace bpt::md_gateway::adapter::okx
