#pragma once

/// \file
/// \brief Bit constants for NewOrder.execInst.
///
/// execInst encodes FIX ExecInst (tag 18) values as an internal
/// bitmask rather than the FIX wire-string-of-codes. Bits combine —
/// e.g. POST_ONLY | REDUCE_ONLY says "fill only as maker AND only if
/// this would reduce my position." 0 = no flags.
///
/// Hand-written, not part of the SBE-regenerated set: SBE generates
/// per-enum/per-message files but we deliberately model this as a
/// uint8 bitmask, which has no SBE enum representation. The schema
/// declares the field as uint8 + a comment listing the bits.

#include <cstdint>

namespace bpt::messages {

/// FIX '6' (ParticipateDontInitiate) — venue rejects the order at
/// submit if it would cross the book. Resting passive only.
inline constexpr std::uint8_t kExecInstPostOnly = 0x01;

// Reserved for future expansion (not yet wired):
//   0x02 — REDUCE_ONLY  (FIX 'F' equivalent — only fill if would reduce position)
//   0x04 — AON          (FIX 'G' equivalent — all-or-none)

}  // namespace bpt::messages
