#pragma once

/// @file
/// SBE codec for VolSurfaceGrid ↔ wire bytes. Pure utility class with no
/// transport dependency — composed by AeronVolSurfacePublisher in prod;
/// a future TapeVolSurfaceRecorder or JsonVolSurfaceDebugFeed reuses it
/// against a different sink. An in-process bus bypasses it entirely.
///
/// Statically testable as a round-trip pair without an Aeron driver
/// (cf. tests/test_sbe_vol_surface_codec.cpp): `decode(encode(x)) == x`.

#include "bpt_common/codec/codec.h"
#include "pricer/surface/vol_surface_grid.h"

#include <cstdint>
#include <span>

namespace bpt::pricer::messaging {

class SbeVolSurfaceCodec {
public:
    /// Encode grid+timestamp into scratch, return the populated subspan.
    /// Caller supplies a buffer ≥ kRecommendedScratchSize. encode() never
    /// allocates.
    ///
    /// The timestamp is passed alongside `grid` (rather than carried by
    /// VolSurfaceGrid itself) to match the existing SBE wire schema where
    /// `timestamp_ns` is a top-level field distinct from the per-point
    /// observations. Codecs that need extra context like this don't
    /// satisfy the strict `Encoder<C, T>` concept — only `Decoder` is
    /// asserted below.
    std::span<const std::byte> encode(const surface::VolSurfaceGrid& grid,
                                      uint64_t timestamp_ns,
                                      std::span<std::byte> scratch);

    /// Decode a complete SBE frame (MessageHeader + body) into a domain
    /// VolSurfaceGrid. The timestamp on the wire is discarded — callers
    /// that need it should add a sibling decode_with_timestamp method
    /// when an in-process subscriber materialises.
    surface::VolSurfaceGrid decode(std::span<const std::byte> bytes);

    /// 56KB fits a worst-case 400-point grid × ~140 bytes/point. Same
    /// upper bound the previous AeronVolSurfacePublisher::publish used.
    static constexpr std::size_t kRecommendedScratchSize = 65536;
};

// Partial conformance check: decode satisfies the canonical signature
// even though encode takes an extra timestamp_ns param. The static_assert
// catches if decode ever drifts; encode's shape is documented as
// "matches Encoder<C, T> with one extra context param" — fine in practice
// because the codec is never invoked through type erasure.
static_assert(bpt::common::codec::Decoder<SbeVolSurfaceCodec, surface::VolSurfaceGrid>);

}  // namespace bpt::pricer::messaging
