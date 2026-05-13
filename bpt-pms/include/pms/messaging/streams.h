#pragma once

// Aeron stream IDs for bpt-pms. Registered in /README.md. 6001 is the
// consolidated balance snapshot from all venues bpt-pms knows about;
// position / PnL / other book slices will land on successive 600x
// streams in later phases.

namespace bpt::pms::messaging {

constexpr int BALANCE_SNAPSHOT_STREAM_ID = 6001;

}  // namespace bpt::pms::messaging
