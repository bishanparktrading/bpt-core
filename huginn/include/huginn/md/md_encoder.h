#pragma once

#include "huginn/md/md_types.h"

#include <bifrost_protocol/MdMarketData.h>
#include <bifrost_protocol/MdOrderBook.h>
#include <bifrost_protocol/MdTrade.h>
#include <bifrost_protocol/MessageHeader.h>

#include <cstddef>

namespace huginn::md {

// Stateless SBE encoder for normalised market-data structs.
//
// All methods are static and write into caller-provided stack buffers, making
// them thread-safe and allocation-free.  The publisher calls encode(), then
// immediately hands the resulting bytes to Aeron — no intermediate copies.
//
// Buffer size constants are exposed so callers can declare exact-fit stack
// arrays without magic numbers.
class MdEncoder {
public:
    // --- BBO ----------------------------------------------------------------
    static constexpr std::size_t kBboBufSize =
        bifrost::protocol::MessageHeader::encodedLength() + bifrost::protocol::MdMarketData::sbeBlockLength();

    // Returns number of bytes written. Always == kBboBufSize on success.
    static std::size_t encode(const MdBbo& bbo, uint64_t seq_num, char* buf, std::size_t capacity) noexcept;

    // --- Trade --------------------------------------------------------------
    static constexpr std::size_t kTradeBufSize =
        bifrost::protocol::MessageHeader::encodedLength() + bifrost::protocol::MdTrade::sbeBlockLength();

    // Returns number of bytes written. Always == kTradeBufSize on success.
    static std::size_t encode(const MdTrade& trade, uint64_t seq_num, char* buf, std::size_t capacity) noexcept;

    // --- OrderBook ----------------------------------------------------------
    // Max supported levels per side. Books larger than this are silently dropped.
    static constexpr std::size_t kMaxLevels = 20;
    // Use a conservatively large fixed buffer rather than computing the exact
    // size via SBE constants, which would require disambiguating bifrost::protocol::MdOrderBook
    // from huginn::md::MdOrderBook in this header.  kMaxLevels * 2 sides * 16 bytes/level
    // + headers is well under 2 KiB; 2048 is safe and stack-friendly.
    static constexpr std::size_t kMaxOrderBookBufSize = 2048;

    // Returns number of bytes written, or 0 if the book exceeds kMaxLevels.
    static std::size_t encode(const MdOrderBook& book, uint64_t seq_num, char* buf, std::size_t capacity) noexcept;
};

}  // namespace huginn::md
