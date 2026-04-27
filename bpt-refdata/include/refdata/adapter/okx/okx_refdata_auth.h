#pragma once

/// \file
/// \brief OKX refdata REST request signer — pure, stateless.
///
/// Uses the same HMAC-SHA256 recipe as order-gw's OKX auth, but separate
/// because refdata's RestClient + header type is a different code path
/// that doesn't depend on Beast.
///
/// \code
///   prehash = timestamp_iso8601 + HTTP_METHOD + request_path (+ body for POSTs)
///   sign    = base64(HMAC-SHA256(secret_key, prehash))
/// \endcode

#include "refdata/http/rest_client.h"

#include <string>

namespace bpt::refdata::adapter {

/// \brief Build the OK-ACCESS-{KEY,SIGN,TIMESTAMP,PASSPHRASE} header block for a REST call.
///
/// \param method     "GET" or "POST".
/// \param target     The request path including any query string.
/// \param simulated  When true, adds `x-simulated-trading: 1` (required for OKX demo-trading).
///
/// No body-signing overload yet — refdata only issues GETs against OKX.
/// Extend if/when a POST is needed.
http::RestClient::Headers okx_auth_headers(const std::string& api_key,
                                           const std::string& secret_key,
                                           const std::string& passphrase,
                                           const std::string& method,
                                           const std::string& target,
                                           bool simulated = false);

}  // namespace bpt::refdata::adapter
