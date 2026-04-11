#pragma once

#include "muninn/mapping/instrument_mapping_loader.h"
#include "muninn/refdata/funding_rate.h"
#include "muninn/refdata/instrument.h"

#include <memory>
#include <string>
#include <vector>

namespace muninn::adapter {

// Pure JSON parser for Hyperliquid REST refdata responses.
// No network I/O, no side effects — suitable for unit testing with fixture data.
class HyperliquidParser {
public:
    explicit HyperliquidParser(std::shared_ptr<mapping::InstrumentMappingLoader> mapping);

    // POST /info {"type":"meta"}
    std::vector<refdata::Instrument> parse_meta(const std::string& body, uint64_t collected_ts) const;

    // POST /info {"type":"userFees","user":"<wallet_address>"}
    std::vector<refdata::FeeScheduleState> parse_user_fees(const std::string& body, uint64_t collected_ts) const;

private:
    std::shared_ptr<mapping::InstrumentMappingLoader> mapping_;
};

}  // namespace muninn::adapter
