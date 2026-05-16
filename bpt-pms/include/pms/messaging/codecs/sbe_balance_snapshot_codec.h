#pragma once

/// @file
/// SBE codec for the PMS BalanceSnapshot stream. Encodes a domain
/// `adapter::BalanceSnapshot` into wire bytes; decode (used by tests
/// and by future in-process consumers) returns the domain struct.

#include "bpt_common/codec/codec.h"
#include "pms/adapter/balance_row.h"

#include <cstddef>
#include <span>

namespace bpt::pms::messaging {

class SbeBalanceSnapshotCodec {
public:
    std::span<const std::byte> encode(const adapter::BalanceSnapshot&, std::span<std::byte> scratch);
    adapter::BalanceSnapshot   decode(std::span<const std::byte>);

    /// 256-row max × ~41 bytes/row + header ≈ 11KB worst case. Round up
    /// to 16KB for safety + alignment.
    static constexpr std::size_t kRecommendedScratchSize = 16384;
};

static_assert(bpt::common::codec::Codec<SbeBalanceSnapshotCodec, adapter::BalanceSnapshot>);

}  // namespace bpt::pms::messaging
