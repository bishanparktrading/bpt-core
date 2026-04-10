#pragma once

#include "surtr/surface/surface_builder.h"

#include <Aeron.h>
#include <FragmentAssembler.h>

#include <bifrost_protocol/ExchangeId.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace surtr::refdata {

struct PerpInstrument {
    uint64_t instrument_id;
    std::string underlying;
    std::string exchange;
    bifrost::protocol::ExchangeId::Value exchange_id;
};

// Subscribes to Sindri refdata streams to discover option instruments.
// Reads snapshot (stream 1001) and deltas (stream 1002).
class RefdataSubscriber {
public:
    using InstrumentCallback = std::function<void(const surface::OptionInstrument& inst)>;
    using PerpCallback = std::function<void(const PerpInstrument& inst)>;
    using RemoveCallback = std::function<void(uint64_t instrument_id)>;

    RefdataSubscriber(std::shared_ptr<aeron::Aeron> aeron,
                      const std::string& snapshot_channel,
                      int32_t snapshot_stream_id,
                      const std::string& delta_channel,
                      int32_t delta_stream_id);

    void set_on_option(InstrumentCallback cb) { on_option_ = std::move(cb); }
    void set_on_perp(PerpCallback cb) { on_perp_ = std::move(cb); }
    void set_on_remove(RemoveCallback cb) { on_remove_ = std::move(cb); }

    // Poll both snapshot and delta subscriptions.
    int poll(int fragment_limit = 10);

private:
    void on_snapshot_fragment(const aeron::concurrent::AtomicBuffer& buffer,
                              aeron::util::index_t offset,
                              aeron::util::index_t length,
                              const aeron::Header& header);
    void on_delta_fragment(const aeron::concurrent::AtomicBuffer& buffer,
                           aeron::util::index_t offset,
                           aeron::util::index_t length,
                           const aeron::Header& header);

    std::shared_ptr<aeron::Subscription> snapshot_sub_;
    std::shared_ptr<aeron::Subscription> delta_sub_;
    std::unique_ptr<aeron::FragmentAssembler> snap_assembler_;
    InstrumentCallback on_option_;
    PerpCallback on_perp_;
    RemoveCallback on_remove_;
};

}  // namespace surtr::refdata
