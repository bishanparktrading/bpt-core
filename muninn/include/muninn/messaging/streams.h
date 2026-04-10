#pragma once

namespace muninn::messaging {

// Instrument refdata (Muninn ↔ Fenrir)
constexpr int REFDATA_SNAPSHOT_STREAM_ID = 1001;  // Muninn → Fenrir (RefDataSnapshot)
constexpr int REFDATA_DELTA_STREAM_ID = 1002;     // Muninn → Fenrir (RefDataDelta, heartbeat)
constexpr int REFDATA_CONTROL_STREAM_ID = 1003;   // Fenrir → Muninn (RefDataSubscriptionRequest)

// Exchange-sourced refdata (Muninn → Fenrir)
constexpr int FEE_SCHEDULE_STREAM_ID = 1004;  // FeeSchedule messages
// Note: stream 1005 (FundingRate) has moved to Huginn — same stream ID, same wire format
constexpr int MUNINN_STATUS_STREAM_ID = 1006;  // RefDataReady + RefDataError messages

}  // namespace muninn::messaging
