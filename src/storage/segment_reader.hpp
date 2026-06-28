#pragma once

#include "storage/segment_writer.hpp"  // AddrEntry
#include "types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pt {

// Parsed segment header (plan §4). `data_offset` is the byte offset within the
// uncompressed segment bytes where the fixed-size records begin.
struct SegmentHeader {
    uint16_t               version      = 0;
    uint16_t               flags        = 0;
    uint64_t               start_utc_ms = 0;
    uint32_t               record_count = 0;  // as stored; may be 0 for a crashed .part
    std::vector<AddrEntry> addrs;
    size_t                 data_offset  = 0;
};

// Reads a segment file into memory, transparently decompressing `.zst`.
// `.part`/`.seg` files are read verbatim. Returns false on I/O or format error.
bool load_segment_bytes(const std::string& path, std::vector<uint8_t>& bytes);

// Parses the header from in-memory uncompressed bytes. Returns false if the
// magic/length is invalid.
bool parse_header(const std::vector<uint8_t>& bytes, SegmentHeader& hdr);

// Number of whole records present after the header, derived from byte length
// (authoritative even when the stored record_count is stale/zero).
uint64_t records_from_size(const std::vector<uint8_t>& bytes, const SegmentHeader& hdr);

// Decodes every record after the header into `out`.
void decode_records(const std::vector<uint8_t>& bytes, const SegmentHeader& hdr,
                    std::vector<RawRecord>& out);

}  // namespace pt
