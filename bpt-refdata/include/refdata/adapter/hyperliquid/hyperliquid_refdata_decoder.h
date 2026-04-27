#pragma once

/// \file
/// \brief Hyperliquid REST refdata response decoder (JSON → refdata structs).

#include "refdata/mapping/instrument_mapping_loader.h"
#include "refdata/refdata/funding_rate.h"
#include "refdata/refdata/instrument.h"

#include <memory>
#include <string>
#include <vector>

namespace bpt::refdata::adapter {

/// \brief Pure JSON decoder for Hyperliquid REST refdata responses.
///
/// No network I/O, no side effects — suitable for unit testing with
/// fixture data.
class HyperliquidRefdataDecoder {
public:
    explicit HyperliquidRefdataDecoder(std::shared_ptr<mapping::InstrumentMappingLoader> mapping);

    /// \brief Decode `POST /info {"type":"meta"}` body.
    std::vector<refdata::Instrument> parse_meta(const std::string& body, uint64_t collected_ts) const;

    /// \brief Decode `POST /info {"type":"userFees","user":"<wallet_address>"}` body.
    std::vector<refdata::FeeScheduleState> parse_user_fees(const std::string& body, uint64_t collected_ts) const;

private:
    std::shared_ptr<mapping::InstrumentMappingLoader> mapping_;
};

}  // namespace bpt::refdata::adapter
