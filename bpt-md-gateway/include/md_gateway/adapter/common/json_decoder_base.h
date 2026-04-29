#pragma once

/// \file
/// \brief Shared simdjson buffer state for venue MD decoders.

#include <cstddef>
#include <cstring>
#include <simdjson.h>
#include <string_view>
#include <vector>

namespace bpt::md_gateway::adapter {

/// \brief Concrete (non-virtual) base providing reusable simdjson parser + padded buffer.
///
/// Each concrete decoder inherits to share the parser instance and
/// SIMDJSON_PADDING-aware copy buffer so we don't reinvent them per venue.
/// Note: NOT a polymorphic base — venue decoders define their own
/// `decode()` signature templated on the publisher type so the hot-path
/// chain stays vtable-free.
class JsonDecoderBase {
protected:
    simdjson::ondemand::parser json_parser_;  ///< simdjson ondemand parser, reused across calls
    std::vector<char> padded_buf_;            ///< copy buffer satisfying simdjson's SIMDJSON_PADDING

    /// \brief Copy payload into padded_buf_, zero-fill the SIMDJSON_PADDING trailing bytes.
    ///
    /// Must be called before each json_parser_.iterate() invocation.
    /// Grows but never shrinks → amortised zero allocation after warmup.
    void pad(std::string_view payload) {
        const std::size_t needed = payload.size() + simdjson::SIMDJSON_PADDING;
        if (padded_buf_.size() < needed)
            padded_buf_.resize(needed);
        std::memcpy(padded_buf_.data(), payload.data(), payload.size());
        std::memset(padded_buf_.data() + payload.size(), 0, simdjson::SIMDJSON_PADDING);
    }
};

}  // namespace bpt::md_gateway::adapter
