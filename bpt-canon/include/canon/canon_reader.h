#pragma once

/// \file
/// \brief CanonReader — minimal sequential reader for `.canon` files.
///
/// Same shape as `bpt::backtester::harness::WslogReader`: header-only,
/// single-threaded, stdio-buffered. `open()` validates the file header
/// (magic + schema version) once; subsequent `next()` calls return one
/// record at a time until EOF.
///
/// Schema-version policy: this reader refuses files whose
/// `schema_version > kSchemaVersion` (newer-than-us). Files with the
/// same major SBE template id and additive event types we don't know
/// about still parse — the unknown event is returned with
/// `type == EventType` cast from the raw byte, and the caller decides
/// what to do (typical: skip and log once).

#include "canon/canon_format.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace bpt::canon {

struct CanonRecord {
    uint64_t ts_ns;
    EventType type;
    std::vector<uint8_t> sbe;
};

class CanonReader {
public:
    explicit CanonReader(const std::string& path) {
        fp_ = std::fopen(path.c_str(), "rb");
        if (fp_ != nullptr)
            open_ok_ = read_and_validate_header();
    }

    ~CanonReader() {
        if (fp_ != nullptr)
            std::fclose(fp_);
    }

    CanonReader(const CanonReader&) = delete;
    CanonReader& operator=(const CanonReader&) = delete;

    /// \brief True iff the file opened *and* the canon header validated
    ///        (magic + non-future schema version).
    [[nodiscard]] bool ok() const noexcept { return fp_ != nullptr && open_ok_; }

    /// \brief Access to the validated file header. Only meaningful if `ok()`.
    [[nodiscard]] const CanonFileHeader& header() const noexcept { return header_; }

    /// \brief Read the next record.
    /// \return std::nullopt at EOF or on a truncated read.
    std::optional<CanonRecord> next() {
        if (!ok())
            return std::nullopt;
        CanonRecordHeader rh{};
        if (std::fread(&rh, sizeof(rh), 1, fp_) != 1)
            return std::nullopt;

        CanonRecord rec;
        rec.ts_ns = rh.ts_ns;
        rec.type = static_cast<EventType>(rh.event_t);
        rec.sbe.resize(rh.sbe_len);
        if (rh.sbe_len > 0 && std::fread(rec.sbe.data(), 1, rh.sbe_len, fp_) != rh.sbe_len)
            return std::nullopt;
        return rec;
    }

private:
    bool read_and_validate_header() {
        if (std::fread(&header_, sizeof(header_), 1, fp_) != 1)
            return false;
        if (std::memcmp(header_.magic, kMagic, sizeof(kMagic)) != 0)
            return false;
        if (header_.schema_version > kSchemaVersion)
            return false;
        if (header_.sbe_template_id != kSbeTemplateFamily)
            return false;
        return true;
    }

    std::FILE* fp_{nullptr};
    bool open_ok_{false};
    CanonFileHeader header_{};
};

}  // namespace bpt::canon
