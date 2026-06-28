#include "storage/segment_reader.hpp"

#include "storage/record_codec.hpp"
#include "util/log.hpp"

#include <zstd.h>

#include <cstring>
#include <fstream>

namespace pt {

namespace {

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    f.seekg(0);
    out.resize(static_cast<size_t>(n));
    if (n > 0 && !f.read(reinterpret_cast<char*>(out.data()), n)) return false;
    return true;
}

bool ends_with(const std::string& s, const char* suf) {
    const size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

uint16_t rd_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
uint32_t rd_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t rd_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

}  // namespace

bool load_segment_bytes(const std::string& path, std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> raw;
    if (!read_file(path, raw)) {
        log_error("cannot read segment '" + path + "'");
        return false;
    }
    if (!ends_with(path, ".zst")) {
        bytes = std::move(raw);
        return true;
    }
    // Decompress a zstd frame.
    const unsigned long long sz = ZSTD_getFrameContentSize(raw.data(), raw.size());
    if (sz == ZSTD_CONTENTSIZE_ERROR || sz == ZSTD_CONTENTSIZE_UNKNOWN) {
        log_error("bad zstd frame in '" + path + "'");
        return false;
    }
    bytes.resize(static_cast<size_t>(sz));
    const size_t got = ZSTD_decompress(bytes.data(), bytes.size(), raw.data(), raw.size());
    if (ZSTD_isError(got) || got != bytes.size()) {
        log_error("zstd decompress failed for '" + path + "'");
        return false;
    }
    return true;
}

bool parse_header(const std::vector<uint8_t>& b, SegmentHeader& hdr) {
    if (b.size() < 22) return false;
    if (b[0] != 'P' || b[1] != 'T' || b[2] != 'S' || b[3] != 'G') return false;
    hdr.version      = rd_u16(&b[4]);
    hdr.flags        = rd_u16(&b[6]);
    hdr.start_utc_ms = rd_u64(&b[8]);
    hdr.record_count = rd_u32(&b[16]);
    const uint16_t target_count = rd_u16(&b[20]);

    size_t pos = 22;
    hdr.addrs.clear();
    hdr.addrs.reserve(target_count);
    for (uint16_t i = 0; i < target_count; ++i) {
        if (pos + 8 > b.size()) return false;
        AddrEntry a;
        a.id   = rd_u16(&b[pos]);     pos += 2;
        a.addr = rd_u32(&b[pos]);     pos += 4;
        const uint16_t name_len = rd_u16(&b[pos]); pos += 2;
        if (pos + name_len > b.size()) return false;
        a.name.assign(reinterpret_cast<const char*>(&b[pos]), name_len);
        pos += name_len;
        hdr.addrs.push_back(std::move(a));
    }
    hdr.data_offset = pos;
    return true;
}

uint64_t records_from_size(const std::vector<uint8_t>& bytes, const SegmentHeader& hdr) {
    if (bytes.size() <= hdr.data_offset) return 0;
    return (bytes.size() - hdr.data_offset) / kRawRecordSize;
}

void decode_records(const std::vector<uint8_t>& bytes, const SegmentHeader& hdr,
                    std::vector<RawRecord>& out) {
    const uint64_t n = records_from_size(bytes, hdr);
    out.reserve(out.size() + static_cast<size_t>(n));
    size_t pos = hdr.data_offset;
    for (uint64_t i = 0; i < n; ++i) {
        out.push_back(decode_record(&bytes[pos]));
        pos += kRawRecordSize;
    }
}

}  // namespace pt
