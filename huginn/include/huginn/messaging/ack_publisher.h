#pragma once

#include "huginn/messaging/i_ack_publisher.h"

#include <Aeron.h>

#include <bifrost_protocol/AckStatus.h>
#include <bifrost_protocol/MdServiceHeartbeat.h>
#include <bifrost_protocol/MdSubscriptionAck.h>
#include <bifrost_protocol/MdSubscriptionHeartbeat.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace huginn::messaging {

// Publishes MdSubscriptionAck, MdSubscriptionHeartbeat, and MdServiceHeartbeat
// on the Huginn→Fenrir ack/heartbeat stream (2003).
//
// Called from both the main poll thread (acks after subscription processing)
// and the service heartbeat timer.  Uses the same aeron::Publication thread-safety
// guarantee as MdPublisher.
class AckPublisher : public IAckPublisher {
public:
    AckPublisher(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel, int stream_id);

    void publish_ack(uint64_t correlation_id,
                     uint64_t instrument_id,
                     const char* exchange,
                     bifrost::protocol::AckStatus::Value status) override;

    void publish_subscription_heartbeat(uint64_t instrument_id) override;

    void publish_service_heartbeat() override;

    [[nodiscard]] uint64_t current_seq() const { return seq_.load(std::memory_order_relaxed); }

private:
    std::shared_ptr<aeron::Publication> publication_;
    std::atomic<uint64_t> seq_{0};
};

}  // namespace huginn::messaging
