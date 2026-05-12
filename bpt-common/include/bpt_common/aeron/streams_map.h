#pragma once

/// \file
/// \brief Shared Aeron-stream registry loaded from `deploy/config/aeron/streams.toml`.
///
/// The trading stack used to declare stream IDs separately in every
/// service's TOML, with the *convention* that matching names matched
/// numbers. That's a footgun: a refdata service whose `[aeron.snapshot]
/// stream_id` no longer matches the strategy's `[aeron.refdata_snapshot]
/// stream_id` silently fails to connect.
///
/// Now every service references the same shared file via its config:
///
///   aeron_config = "deploy/config/aeron/streams.toml"
///
/// and looks up its own streams by **global name** (`refdata_snapshot`,
/// `md_data`, …) — see `resolve_stream` below. Drift becomes impossible.
///
/// Format of `streams.toml`:
///
///   [aeron]
///   media_driver_dir = "/dev/shm/aeron-bpt"   # universal
///
///   [streams]
///   refdata_snapshot = 1001
///   md_data          = 2002
///   ...
///
///   [channels]
///   # Per-stream channel override; empty by default (everything uses
///   # "aeron:ipc"). Add entries only when you need UDP / multicast.

#include "bpt_common/aeron/stream_config.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace bpt::common::config {

/// \brief Parsed contents of streams.toml.
struct AeronStreamMap {
    /// `[aeron] media_driver_dir = …` — universal MediaDriver path.
    /// Empty string if not present (caller falls back to its own default).
    std::string media_driver_dir;

    /// `[streams]` table — global name → stream_id.
    std::unordered_map<std::string, int32_t> stream_ids;

    /// `[channels]` table — global name → channel URI. Optional; entries
    /// missing here resolve to "aeron:ipc" via `resolve_stream`.
    std::unordered_map<std::string, std::string> channels;
};

/// \brief Parse a streams.toml file. Throws `std::runtime_error` on
///        parse failure (malformed TOML, file not found). Missing
///        `[streams]` / `[channels]` tables yield empty maps — that's
///        not a parse error, just an empty registry.
[[nodiscard]] AeronStreamMap load_shared_streams(const std::string& path);

/// \brief Resolve a single stream by global name.
///
/// Priority: shared map entry → fallback (id + "aeron:ipc"). If the
/// shared map doesn't contain the name, a `log::warn` fires and the
/// fallback is used — services still start, but the operator sees the
/// misconfiguration in the journal.
[[nodiscard]] StreamConfig resolve_stream(const AeronStreamMap& shared,
                                          std::string_view global_name,
                                          int32_t fallback_id,
                                          std::string_view fallback_channel = "aeron:ipc");

}  // namespace bpt::common::config
