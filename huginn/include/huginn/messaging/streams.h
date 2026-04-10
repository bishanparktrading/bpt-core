#pragma once

namespace huginn::messaging {

// Aeron stream IDs for Huginn ↔ Fenrir communication.
// Stream 2001: Fenrir → Huginn  (MdSubscribeBatch)
// Stream 2002: Huginn → Fenrir  (MdMarketData, MdTrade)
// Stream 2003: Huginn → Fenrir  (MdSubscriptionAck, MdSubscriptionHeartbeat, MdServiceHeartbeat)
// Stream 1005: Huginn → Fenrir  (FundingRate) — moved from Muninn; same stream ID, same wire format
constexpr int MD_CONTROL_STREAM_ID = 2001;
constexpr int MD_DATA_STREAM_ID = 2002;
constexpr int MD_ACK_HB_STREAM_ID = 2003;
constexpr int FUNDING_RATE_STREAM_ID = 1005;

}  // namespace huginn::messaging
