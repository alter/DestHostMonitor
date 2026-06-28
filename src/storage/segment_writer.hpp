#pragma once

#include "types.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pt {

// One entry of a segment's address table (plan §4 header).
struct AddrEntry {
    uint16_t    id   = 0;
    uint32_t    addr = 0;  // IPv4 network order, 0 if hostname-only / unresolved
    std::string name;
};

// Appends raw 12-byte records to an active `seg_<startutc>.part` file, with a
// header carrying the segment start time and the address table. Stage 2 keeps a
// single active segment; stage 3 adds hourly sealing, zstd and the index.
class SegmentWriter {
public:
    SegmentWriter() = default;
    ~SegmentWriter();

    SegmentWriter(const SegmentWriter&) = delete;
    SegmentWriter& operator=(const SegmentWriter&) = delete;

    // Creates `dir` if needed and opens a new active segment. Returns false on
    // failure. `start_utc_ms` becomes the segment's t_offset origin.
    bool open(const std::string& dir, uint64_t start_utc_ms,
              const std::vector<AddrEntry>& addrs);

    bool is_open() const { return file_ != nullptr; }
    uint64_t start_utc_ms() const { return start_utc_ms_; }
    uint64_t record_count() const { return record_count_; }
    const std::string& path() const { return path_; }

    // Appends one record. Returns false on write error.
    bool append(const RawRecord& r);

    // Flushes buffered records to the OS and fsyncs.
    void flush();

    // Flushes, patches the record count into the header, and closes the file.
    // The `.part` file is left on disk (sealing to .zst arrives in stage 3).
    void close();

private:
    std::FILE*  file_         = nullptr;
    std::string path_;
    uint64_t    start_utc_ms_ = 0;
    uint64_t    record_count_ = 0;
    long        count_offset_ = 0;  // byte offset of record_count field in header
};

}  // namespace pt
