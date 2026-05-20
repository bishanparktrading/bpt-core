#include "canon/canon_writer.h"

#include "bpt_common/logging.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>

namespace bpt::canon {

namespace fs = std::filesystem;

CanonWriter::CanonWriter(Config cfg) : cfg_(std::move(cfg)) {
    if (cfg_.buffer_bytes == 0)
        cfg_.buffer_bytes = 1u << 20;
    buffer_.resize(cfg_.buffer_bytes);
}

CanonWriter::~CanonWriter() {
    close();
}

bool CanonWriter::open() {
    if (fp_ != nullptr)
        return true;

    std::error_code ec;
    fs::create_directories(fs::path(cfg_.path).parent_path(), ec);
    if (ec) {
        bpt::common::log::error("CanonWriter: create_directories({}) failed: {} ({})",
                                fs::path(cfg_.path).parent_path().string(),
                                ec.message(),
                                ec.value());
        return false;
    }

    fp_ = std::fopen(cfg_.path.c_str(), "wb");
    if (fp_ == nullptr) {
        bpt::common::log::error("CanonWriter: fopen({}, \"wb\") failed: {} (errno={})",
                                cfg_.path,
                                std::strerror(errno),
                                errno);
        return false;
    }

    return write_file_header();
}

bool CanonWriter::write_file_header() {
    CanonFileHeader hdr{};
    std::memcpy(hdr.magic, kMagic, sizeof(kMagic));
    hdr.schema_version = kSchemaVersion;
    hdr.sbe_template_id = kSbeTemplateFamily;
    hdr.venue_id = cfg_.venue_id;
    hdr.flags = 0;
    hdr.date_utc = cfg_.date_utc;

    // Null-padded ASCII fields. memcpy up to field size — both fields are
    // documented as null-padded, not null-terminated, so consumers respect
    // the fixed length.
    const std::size_t kind_n = std::min(cfg_.producer_kind.size(), sizeof(hdr.producer_kind));
    std::memcpy(hdr.producer_kind, cfg_.producer_kind.data(), kind_n);
    const std::size_t sha_n = std::min(cfg_.producer_sha.size(), sizeof(hdr.producer_sha));
    std::memcpy(hdr.producer_sha, cfg_.producer_sha.data(), sha_n);

    if (std::fwrite(&hdr, sizeof(hdr), 1, fp_) != 1) {
        bpt::common::log::error("CanonWriter: fwrite(file_header, {} B) to {} failed: {} (errno={})",
                                sizeof(hdr),
                                cfg_.path,
                                std::strerror(errno),
                                errno);
        std::fclose(fp_);
        fp_ = nullptr;
        return false;
    }
    bytes_written_.fetch_add(sizeof(hdr), std::memory_order_relaxed);
    return true;
}

bool CanonWriter::flush_buffer_to_fp() {
    if (buffer_pos_ == 0)
        return true;
    if (std::fwrite(buffer_.data(), 1, buffer_pos_, fp_) != buffer_pos_) {
        bpt::common::log::error("CanonWriter: fwrite(buffer={} B) to {} failed: {} (errno={})",
                                buffer_pos_,
                                cfg_.path,
                                std::strerror(errno),
                                errno);
        return false;
    }
    buffer_pos_ = 0;
    return true;
}

bool CanonWriter::write_event(uint64_t ts_ns, EventType type, std::string_view sbe) {
    if (fp_ == nullptr)
        return false;
    if (sbe.size() > UINT16_MAX) {
        // SBE messages we care about are all well under 64 KiB; if this
        // ever fires the schema layout has drifted and we'd rather find
        // out at write time than corrupt the file.
        bpt::common::log::error("CanonWriter: sbe payload {} B exceeds u16 length field (path={})",
                                sbe.size(),
                                cfg_.path);
        return false;
    }

    CanonRecordHeader rh{};
    rh.ts_ns = ts_ns;
    rh.event_t = static_cast<uint8_t>(type);
    rh.sbe_len = static_cast<uint16_t>(sbe.size());

    const std::size_t total = sizeof(rh) + sbe.size();

    if (buffer_pos_ + total > buffer_.size()) {
        if (!flush_buffer_to_fp())
            return false;
        // Pathological-size record: write direct (same fall-through Tape
        // uses — keeps the buffer bound to a sensible size even when one
        // event is larger than the buffer, which can happen for big L2
        // snapshots).
        if (total > buffer_.size()) {
            if (std::fwrite(&rh, sizeof(rh), 1, fp_) != 1) {
                bpt::common::log::error("CanonWriter: fwrite(rec_header direct) to {} failed: {} (errno={})",
                                        cfg_.path,
                                        std::strerror(errno),
                                        errno);
                return false;
            }
            if (rh.sbe_len > 0 && std::fwrite(sbe.data(), 1, sbe.size(), fp_) != sbe.size()) {
                bpt::common::log::error("CanonWriter: fwrite(sbe direct, {} B) to {} failed: {} (errno={})",
                                        sbe.size(),
                                        cfg_.path,
                                        std::strerror(errno),
                                        errno);
                return false;
            }
            events_written_.fetch_add(1, std::memory_order_relaxed);
            bytes_written_.fetch_add(total, std::memory_order_relaxed);
            return true;
        }
    }

    std::memcpy(buffer_.data() + buffer_pos_, &rh, sizeof(rh));
    buffer_pos_ += sizeof(rh);
    if (rh.sbe_len > 0) {
        std::memcpy(buffer_.data() + buffer_pos_, sbe.data(), sbe.size());
        buffer_pos_ += sbe.size();
    }

    events_written_.fetch_add(1, std::memory_order_relaxed);
    bytes_written_.fetch_add(total, std::memory_order_relaxed);
    return true;
}

void CanonWriter::flush() {
    if (fp_ == nullptr)
        return;
    flush_buffer_to_fp();
    std::fflush(fp_);
}

void CanonWriter::close() {
    if (fp_ == nullptr)
        return;
    flush_buffer_to_fp();
    std::fflush(fp_);
    std::fclose(fp_);
    fp_ = nullptr;
}

}  // namespace bpt::canon
