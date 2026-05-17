#pragma once

/// \file
/// Port: refdata subscriber. Discovers option instruments from
/// bpt-refdata snapshot + delta streams; surfaces perp metadata too
/// (used to map FundingRate streams to underlyings).

#include "pricer/surface/surface_builder.h"

#include <messages/ExchangeId.h>

#include <cstdint>
#include <functional>
#include <string>

namespace bpt::pricer::refdata {

struct PerpInstrument {
    uint64_t instrument_id;
    std::string underlying;
    std::string exchange;
    bpt::messages::ExchangeId::Value exchange_id;
    std::string venue_symbol;  ///< e.g. "BTC-PERPETUAL"; needed by MdSubscribeBatch
};

namespace api {

class RefdataSubscriber {
public:
    using InstrumentCallback = std::function<void(const surface::OptionInstrument& inst)>;
    using PerpCallback = std::function<void(const PerpInstrument& inst)>;
    using RemoveCallback = std::function<void(uint64_t instrument_id)>;

    virtual ~RefdataSubscriber() = default;

    void set_on_option(InstrumentCallback cb) { on_option_ = std::move(cb); }
    void set_on_perp(PerpCallback cb) { on_perp_ = std::move(cb); }
    void set_on_remove(RemoveCallback cb) { on_remove_ = std::move(cb); }

    /// Publish a RefDataSubscriptionRequest on the control stream to ask
    /// bpt-refdata to push the current snapshot. Requires the control
    /// publication to have been configured via the constructor; no-op
    /// otherwise.
    virtual void send_subscription_request(uint64_t correlation_id) = 0;

    /// Poll both snapshot and delta subscriptions.
    virtual int poll(int fragment_limit = 10) = 0;

protected:
    InstrumentCallback on_option_;
    PerpCallback on_perp_;
    RemoveCallback on_remove_;
};

}  // namespace api

}  // namespace bpt::pricer::refdata
