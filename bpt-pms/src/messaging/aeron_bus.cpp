#include "pms/messaging/aeron_bus.h"

#include "pms/config/settings.h"
#include "pms/messaging/publishers/aeron_balance_snapshot_publisher.h"

namespace bpt::pms::messaging {

PmsBus PmsAeronBus::build(std::shared_ptr<aeron::Aeron> aeron, const config::Settings& settings) {
    PmsBus bus;
    bus.snapshot_pub = std::make_unique<AeronBalanceSnapshotPublisher>(std::move(aeron),
                                                                       settings.aeron.balance_snapshot.channel,
                                                                       settings.aeron.balance_snapshot.stream_id);
    return bus;
}

}  // namespace bpt::pms::messaging
