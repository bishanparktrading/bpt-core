#pragma once

namespace heimdall::messaging {

constexpr int ORDER_STREAM_ID = 3001;             // Fenrir -> Heimdall
constexpr int EXEC_REPORT_STREAM_ID = 3002;       // Heimdall -> Fenrir
constexpr int HEARTBEAT_STREAM_ID = 3003;         // Heimdall -> Fenrir
constexpr int ACCOUNT_SNAPSHOT_STREAM_ID = 3004;  // Heimdall -> Fenrir

}  // namespace heimdall::messaging
