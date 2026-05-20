#pragma once

/// \file
/// \brief SBE encode/decode helpers for canon events.
///
/// Canon record payloads are the same wire bytes Aeron carries on the
/// live system: MessageHeader + SBE block. These helpers encode/decode
/// without touching Aeron, so producers (wslog-replay, OKX-archive)
/// and consumers (deterministic backtester) can share the bytes
/// bit-identically.
///
/// Each encode_* returns the number of bytes written, or 0 if the
/// destination buffer is too small. Caller picks the buffer:
/// `CanonScratch::kFundingSize` / `kBboSize` / `kTradeSize` /
/// `kBookSize` are stack-friendly upper bounds.

#include "canon/canon_format.h"
#include "md_gateway/md/md_types.h"
#include "md_gateway/messaging/publishers/api/funding_rate_publisher.h"  // FundingRateUpdate

#include <cstddef>
#include <cstdint>

namespace bpt::canon {

/// Buffer-size hints (stack-allocate the upper bound — every event fits).
struct CanonScratch {
    static constexpr std::size_t kBboSize = 128;
    static constexpr std::size_t kTradeSize = 128;
    static constexpr std::size_t kBookSize = 2048;  // matches MdEncoder::kMaxOrderBookBufSize
    static constexpr std::size_t kFundingSize = 128;
    static constexpr std::size_t kMaxAnySize = kBookSize;
};

/// \return bytes written (MessageHeader + MdMarketData block), 0 on overflow.
std::size_t encode_bbo(const bpt::md_gateway::md::MdBbo& bbo,
                       uint64_t seq_num,
                       char* buf,
                       std::size_t capacity) noexcept;

/// \return bytes written (MessageHeader + MdTrade block), 0 on overflow.
std::size_t encode_trade(const bpt::md_gateway::md::MdTrade& trade,
                         uint64_t seq_num,
                         char* buf,
                         std::size_t capacity) noexcept;

/// Delegates to bpt::md_gateway::md::MdEncoder so the on-disk bytes match
/// what the live MdPublisher would emit. \return bytes written, 0 on
/// overflow or oversize book.
std::size_t encode_book(const bpt::md_gateway::md::MdOrderBook& book,
                        uint64_t seq_num,
                        char* buf,
                        std::size_t capacity) noexcept;

/// \return bytes written (MessageHeader + FundingRate block), 0 on overflow.
std::size_t encode_funding(const bpt::md_gateway::messaging::FundingRateUpdate& fr,
                           char* buf,
                           std::size_t capacity) noexcept;

/// \name Decoders — mirror image of the encoders above.
///
/// These take SBE bytes (as produced by encode_* or by the live
/// `MdPublisher` on Aeron) and populate domain types. Used by the
/// deterministic backtester to consume canon: SBE blob → MdBbo →
/// HarnessMdPublisher::publish() reuses the existing matching-engine
/// fan-out without going back through the JSON decoder.
///
/// All decoders return false on a malformed/truncated input (wrong
/// template id, buffer too small for the declared block length).
/// On true the out-parameter is fully populated.
/// @{
[[nodiscard]] bool decode_bbo(const char* buf, std::size_t len, bpt::md_gateway::md::MdBbo& out) noexcept;
[[nodiscard]] bool decode_trade(const char* buf, std::size_t len, bpt::md_gateway::md::MdTrade& out) noexcept;
[[nodiscard]] bool decode_book(const char* buf, std::size_t len, bpt::md_gateway::md::MdOrderBook& out) noexcept;
[[nodiscard]] bool decode_funding(const char* buf,
                                  std::size_t len,
                                  bpt::md_gateway::messaging::FundingRateUpdate& out) noexcept;
/// @}

}  // namespace bpt::canon
