#pragma once

#include <bifrost_protocol/InstrumentType.h>

#include <cstdint>
#include <vector>

namespace muninn::messaging {

// One instrument filter decoded from a RefDataSubscriptionRequest.
// symbol or exchange left as all-zeros means "no filter on that field".
struct InstrumentFilter {
    char symbol[24];   // venue symbol, null-padded
    char exchange[8];  // exchange/venue name, null-padded
};

// Canonical filter entry from the canonicalFilter group of RefDataSubscriptionRequest.
// Muninn matches on base/quote/type semantics rather than venue-specific symbols.
// An exchange field of all-zeros means "match any exchange".
struct CanonicalFilter {
    char base_currency[8];
    char quote_currency[8];
    bifrost::protocol::InstrumentType::Value instrument_type;
    char exchange[8];
};

// Internal representation of a decoded RefDataSubscriptionRequest.
struct RefdataRequest {
    uint64_t correlation_id;
    std::vector<InstrumentFilter> instruments;       // empty = match all (legacy)
    std::vector<CanonicalFilter> canonical_filters;  // empty = no canonical filter
};

}  // namespace muninn::messaging
