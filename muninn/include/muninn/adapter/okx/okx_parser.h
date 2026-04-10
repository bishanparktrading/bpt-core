#pragma once

#include "muninn/http/rest_client.h"
#include "muninn/mapping/instrument_mapping_loader.h"
#include "muninn/refdata/funding_rate.h"
#include "muninn/refdata/instrument.h"

#include <memory>
#include <string>
#include <vector>

namespace muninn::adapter {

// Pure JSON parser for OKX REST refdata responses.
// No network I/O, no side effects — suitable for unit testing with fixture data.
class OKXParser {
public:
    explicit OKXParser(std::shared_ptr<mapping::InstrumentMappingLoader> mapping);

    // GET /api/v5/public/instruments?instType=SPOT|SWAP|FUTURES
    std::vector<refdata::Instrument> parse_instruments(const std::string& body,
                                                       const std::string& inst_type,
                                                       uint64_t collected_ts) const;

    // GET /api/v5/account/trade-fee?instType=SPOT|SWAP
    std::vector<refdata::FeeScheduleState> parse_trade_fee(const std::string& body,
                                                           uint64_t collected_ts) const;

private:
    std::shared_ptr<mapping::InstrumentMappingLoader> mapping_;
};

// Computes the OKX request-signing headers for a private endpoint.
// method should be "GET" or "POST".
http::RestClient::Headers okx_auth_headers(const std::string& api_key,
                                           const std::string& secret_key,
                                           const std::string& passphrase,
                                           const std::string& method,
                                           const std::string& target,
                                           bool simulated = false);

}  // namespace muninn::adapter
