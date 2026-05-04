#pragma once

/// \file
/// \brief Stateless SBE encoder for variable-length MD types.
///
/// BBO and Trade are encoded inline in MdPublisher via the zero-copy
/// `publish<T>` path. OrderBook still goes through this helper because
/// its payload size is dynamic (per-side level count) and the
/// fixed-size `tryClaim` fast path doesn't fit.

#include "md_gateway/md/md_types.h"

#include <messages/MdOrderBook.h>
#include <messages/MessageHeader.h>

#include <cstddef>

namespace bpt::md_gateway::md {

/// \brief SBE encoder for MdOrderBook.
class MdEncoder {
public:
    /// \brief Max supported levels per side.
    ///
    /// Aliased to the domain-side constant in `md_types.h` so the
    /// InlineVec capacity and the encoder's per-side cap can never
    /// drift apart. Books with more levels are silently dropped
    /// (encode returns 0).
    static constexpr std::size_t kMaxLevels = kMaxBookLevels;

    /// \brief Upper bound on encoded MdOrderBook payload size in bytes.
    ///
    /// kMaxLevels * 2 sides * 16 bytes/level + headers — fits well
    /// under 2 KiB so callers can stack-allocate the claim buffer.
    static constexpr std::size_t kMaxOrderBookBufSize = 2048;

    /// \brief Encode `book` into `buf` and return bytes written.
    /// \return number of bytes written, or 0 if the book exceeds kMaxLevels
    static std::size_t encode(const MdOrderBook& book, uint64_t seq_num, char* buf, std::size_t capacity) noexcept;
};

}  // namespace bpt::md_gateway::md
