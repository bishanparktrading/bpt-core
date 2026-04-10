#pragma once

#include <Aeron.h>
#include <FragmentAssembler.h>
#include <bifrost_protocol/InstrumentType.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "heimdall/refdata/instrument.h"
#include "heimdall/refdata/instrument_cache.h"

namespace heimdall::refdata {

class RefdataClient {
public:
    using OnSnapshotFn = std::function<void(const InstrumentCache&)>;
    using OnDeltaFn =
        std::function<void(const Instrument&, bifrost::protocol::DeltaUpdateType::Value)>;

    // Canonical filter entry to pre-filter the Sindri snapshot server-side.
    // An empty exchange means "any exchange".
    struct CanonicalFilter {
        std::string base;
        std::string quote;
        bifrost::protocol::InstrumentType::Value instrument_type;
        std::string exchange;
    };

    RefdataClient(std::shared_ptr<aeron::Aeron> aeron, const std::string& channel,
                  int control_stream, int snapshot_stream, int delta_stream);

    // Send a subscription request. Empty filters vector means subscribe-all.
    void subscribe(uint64_t correlation_id, std::vector<CanonicalFilter> filters = {});

    // Poll both streams. Returns total fragment count processed.
    int poll(int fragment_limit = 10);

    // Fired once after the snapshot is fully received.
    OnSnapshotFn on_snapshot_complete;

    // Fired for every delta received after snapshot.
    OnDeltaFn on_delta;

    // Fired when a delta sequence gap is detected.
    std::function<void()> on_gap_detected;

    [[nodiscard]] uint64_t last_heartbeat_ns() const { return last_heartbeat_ns_; }
    [[nodiscard]] const InstrumentCache& cache() const { return cache_; }

private:
    void handle_snapshot_fragment(aeron::AtomicBuffer& buffer, aeron::util::index_t offset,
                                  aeron::util::index_t length, aeron::Header& header);

    void handle_delta_fragment(aeron::AtomicBuffer& buffer, aeron::util::index_t offset,
                               aeron::util::index_t length, aeron::Header& header);

    std::shared_ptr<aeron::Publication> ctrl_pub_;
    std::shared_ptr<aeron::Subscription> snap_sub_;
    std::shared_ptr<aeron::Subscription> delta_sub_;
    std::unique_ptr<aeron::FragmentAssembler> snap_assembler_;
    std::unique_ptr<aeron::FragmentAssembler> delta_assembler_;
    InstrumentCache cache_;
    uint64_t correlation_id_{0};
    uint64_t last_heartbeat_ns_{0};
};

}  // namespace heimdall::refdata
