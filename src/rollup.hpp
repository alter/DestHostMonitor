#pragma once

#include "types.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pt {

// Online per-minute aggregation. Samples are added as they flow through the
// writer; completed minutes are flushed to rollup/<YYYY-MM-DD>.csv (UTC).
// Kept forever (plan §5) so outage hunting never needs the raw segments.
class RollupWriter {
public:
    RollupWriter(std::string storage_dir, std::map<uint16_t, std::string> names);

    void add(const ProbeSample& s);

    // Flushes every buffered minute strictly older than the minute of `now_utc`.
    void flush_completed(uint64_t now_utc);

    // Flushes all remaining buffered minutes (call on shutdown).
    void finalize();

private:
    struct Bucket {
        uint64_t              sent = 0;
        uint64_t              lost = 0;
        std::vector<uint16_t> rtt;  // tenths of ms, OK samples only
    };

    void write_minute(uint64_t minute_utc, uint16_t target_id, Bucket& b);
    std::string path_for(uint64_t minute_utc) const;

    std::string                                        dir_;
    std::map<uint16_t, std::string>                    names_;
    std::map<uint64_t, std::map<uint16_t, Bucket>>     buckets_;  // minute -> target -> bucket
};

}  // namespace pt
