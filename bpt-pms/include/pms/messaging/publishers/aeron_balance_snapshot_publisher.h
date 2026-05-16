#pragma once

/// @file
/// Aeron+SBE implementation of IBalanceSnapshotPublisher. Composes
/// SbeBalanceSnapshotCodec for serialisation.

#include "pms/messaging/codecs/sbe_balance_snapshot_codec.h"
#include "pms/messaging/publishers/i_balance_snapshot_publisher.h"

#include <Aeron.h>

#include <bpt_common/aeron/publisher.h>
#include <memory>
#include <string>

namespace bpt::pms::messaging {

class AeronBalanceSnapshotPublisher : public IBalanceSnapshotPublisher {
public:
    AeronBalanceSnapshotPublisher(std::shared_ptr<::aeron::Aeron> aeron,
                                  const std::string& channel,
                                  int stream_id);

    void publish(const adapter::BalanceSnapshot& snapshot) override;

private:
    bpt::common::aeron::Publisher publisher_;
    SbeBalanceSnapshotCodec       codec_;
};

}  // namespace bpt::pms::messaging
