#include "pms/messaging/publishers/aeron_balance_snapshot_publisher.h"

#include <bpt_common/logging.h>
#include <cstddef>

namespace bpt::pms::messaging {

using Policy = bpt::common::aeron::Publisher::Policy;

AeronBalanceSnapshotPublisher::AeronBalanceSnapshotPublisher(std::shared_ptr<::aeron::Aeron> aeron,
                                                             const std::string& channel,
                                                             int stream_id)
    // BalanceSnapshot is idempotent — next poll replaces stale data —
    // so kBoundedRetry on back-pressure + drop on no-subscriber is the
    // right shape.
    : publisher_(std::move(aeron), channel, stream_id, Policy::kBoundedRetry) {}

void AeronBalanceSnapshotPublisher::publish(const adapter::BalanceSnapshot& snapshot) {
    alignas(8) std::byte scratch[SbeBalanceSnapshotCodec::kRecommendedScratchSize];
    const auto bytes = codec_.encode(snapshot, scratch);

    const auto len = static_cast<::aeron::util::index_t>(bytes.size());
    ::aeron::AtomicBuffer ab(reinterpret_cast<uint8_t*>(scratch), len);

    if (publisher_.offer(ab, 0, len))
        bpt::common::log::info("BalanceSnapshot published rows={}", snapshot.rows.size());
    else
        bpt::common::log::debug("BalanceSnapshot drop rows={} (no subscriber / back-pressure)", snapshot.rows.size());
}

}  // namespace bpt::pms::messaging
