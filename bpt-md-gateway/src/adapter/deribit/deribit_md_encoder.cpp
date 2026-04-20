#include "md_gateway/adapter/deribit/deribit_md_encoder.h"

#include <fmt/format.h>

namespace bpt::md_gateway::adapter::deribit {

std::string build_subscribe_rpc(uint64_t rpc_id, const std::string& symbol, uint8_t depth) {
    const std::string book_channel =
        (depth == 0) ? fmt::format("quote.{}", symbol) : fmt::format("book.{}.100ms", symbol);

    return fmt::format(
        R"({{"jsonrpc":"2.0","id":{},"method":"public/subscribe","params":{{"channels":["trades.{}.100ms","{}"]}}}})",
        rpc_id,
        symbol,
        book_channel);
}

std::string build_set_heartbeat_rpc(uint64_t rpc_id, int interval_s) {
    return fmt::format(
        R"({{"jsonrpc":"2.0","id":{},"method":"public/set_heartbeat","params":{{"interval":{}}}}})",
        rpc_id,
        interval_s);
}

std::string build_test_response_rpc(uint64_t rpc_id) {
    return fmt::format(R"({{"jsonrpc":"2.0","id":{},"method":"public/test","params":{{}}}})", rpc_id);
}

}  // namespace bpt::md_gateway::adapter::deribit
