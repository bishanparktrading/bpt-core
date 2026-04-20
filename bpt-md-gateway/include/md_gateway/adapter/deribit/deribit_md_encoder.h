#pragma once

// Deribit market-data JSON-RPC envelope builders — pure input → JSON,
// no state, no I/O. Counterpart to deribit_parser (wire → internal).
//
// Deribit's market-data WebSocket uses JSON-RPC 2.0. The envelope shape
// is uniform (jsonrpc/id/method/params) so each builder just fills in
// the method + params for its specific call.

#include <cstdint>
#include <string>

namespace bpt::md_gateway::adapter::deribit {

// public/subscribe on book + trades channels for one instrument.
// depth == 0 uses the lighter quote.<sym> channel; depth > 0 uses
// book.<sym>.100ms for full-depth ladder updates at 100ms cadence.
[[nodiscard]] std::string build_subscribe_rpc(uint64_t rpc_id,
                                               const std::string& symbol,
                                               uint8_t depth);

// public/set_heartbeat — Deribit tears down the session within 30s if
// test_request is not answered with public/test, so this must be sent
// immediately after connect.
[[nodiscard]] std::string build_set_heartbeat_rpc(uint64_t rpc_id, int interval_s);

// public/test — reply to Deribit's heartbeat test_request.
[[nodiscard]] std::string build_test_response_rpc(uint64_t rpc_id);

}  // namespace bpt::md_gateway::adapter::deribit
