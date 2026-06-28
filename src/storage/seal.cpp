#include "storage/seal.hpp"

#include "storage/segment_reader.hpp"
#include "util/log.hpp"

#include <zstd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace pt {

namespace fs = std::filesystem;

namespace {

constexpr int kZstdLevel = 12;

void patch_u32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off + 0] = static_cast<uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

bool write_file(const std::string& path, const uint8_t* data, size_t n) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
    return static_cast<bool>(f);
}

std::string targets_csv(const SegmentHeader& hdr) {
    std::string s;
    for (const auto& a : hdr.addrs) {
        if (!s.empty()) s += ';';
        s += std::to_string(a.id);
    }
    return s;
}

}  // namespace

bool seal_part(const std::string& dir, const std::string& part_path,
               uint64_t end_utc, Index& index) {
    std::vector<uint8_t> bytes;
    if (!load_segment_bytes(part_path, bytes)) return false;

    SegmentHeader hdr;
    if (!parse_header(bytes, hdr)) {
        log_error("seal: bad header in '" + part_path + "'");
        return false;
    }

    // Authoritative record count from byte length; patch the header so the
    // sealed file is self-consistent even if the writer crashed before close().
    const uint64_t count = records_from_size(bytes, hdr);
    patch_u32(bytes, 16, static_cast<uint32_t>(count));

    // Trim any partial trailing record (truncated write at crash).
    const size_t clean_size = hdr.data_offset + static_cast<size_t>(count) * kRawRecordSize;
    bytes.resize(clean_size);

    if (end_utc == 0) {
        std::vector<RawRecord> recs;
        decode_records(bytes, hdr, recs);
        end_utc = hdr.start_utc_ms;
        for (const auto& r : recs) end_utc = std::max(end_utc, hdr.start_utc_ms + r.t_offset_ms);
    }

    // Compress.
    const size_t bound = ZSTD_compressBound(bytes.size());
    std::vector<uint8_t> comp(bound);
    const size_t csz = ZSTD_compress(comp.data(), comp.size(),
                                     bytes.data(), bytes.size(), kZstdLevel);
    if (ZSTD_isError(csz)) {
        log_error(std::string("seal: zstd compress failed: ") + ZSTD_getErrorName(csz));
        return false;
    }

    const std::string base = "seg_" + std::to_string(hdr.start_utc_ms) + ".zst";
    const std::string out_path = dir + "/" + base;
    if (!write_file(out_path, comp.data(), csz)) {
        log_error("seal: cannot write '" + out_path + "'");
        return false;
    }

    IndexEntry e;
    e.start_utc    = hdr.start_utc_ms;
    e.end_utc      = end_utc;
    e.file         = base;
    e.record_count = count;
    e.targets      = targets_csv(hdr);
    index.append(e);

    std::error_code ec;
    fs::remove(part_path, ec);

    log_info("sealed " + base + " (" + std::to_string(count) + " records, " +
             std::to_string(bytes.size()) + " -> " + std::to_string(csz) + " bytes)");
    return true;
}

size_t recover_dangling_parts(const std::string& dir, Index& index) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return 0;

    std::vector<std::string> parts;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!de.is_regular_file()) continue;
        const std::string name = de.path().filename().string();
        if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".part") == 0 &&
            name.compare(0, 4, "seg_") == 0) {
            parts.push_back(de.path().string());
        }
    }

    size_t n = 0;
    for (const auto& p : parts) {
        log_warn("recovering dangling segment '" + p + "'");
        if (seal_part(dir, p, /*end_utc=*/0, index)) ++n;
    }
    return n;
}

size_t enforce_retention(const std::string& dir, int retention_days,
                         uint64_t now_utc_ms, Index& index) {
    if (retention_days <= 0) return 0;
    const uint64_t span = static_cast<uint64_t>(retention_days) * 86400000ULL;
    if (now_utc_ms < span) return 0;
    const uint64_t cutoff = now_utc_ms - span;

    size_t removed = 0;
    for (const auto& e : index.load()) {
        if (e.end_utc >= cutoff) continue;
        std::error_code ec;
        if (fs::remove(dir + "/" + e.file, ec)) {
            ++removed;
            log_info("retention: removed " + e.file);
        }
    }
    if (removed > 0) index.rebuild();
    return removed;
}

}  // namespace pt
