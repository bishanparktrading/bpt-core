#pragma once

namespace bpt::order_gateway::messaging {

constexpr int ORDER_STREAM_ID = 3001;             // Strategy -> OrderGateway
constexpr int EXEC_REPORT_STREAM_ID = 3002;       // OrderGateway -> Strategy
constexpr int HEARTBEAT_STREAM_ID = 3003;         // OrderGateway -> Strategy
constexpr int ACCOUNT_SNAPSHOT_STREAM_ID = 3004;  // OrderGateway -> Strategy

}  // namespace bpt::order_gateway::messaging
