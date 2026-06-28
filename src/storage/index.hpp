#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pt {

// One row of index.csv (plan §4).
struct IndexEntry {
    uint64_t    start_utc    = 0;
    uint64_t    end_utc      = 0;
    std::string file;          // basename, e.g. seg_<start>.zst
    uint64_t    record_count  = 0;
    std::string targets;       // ';'-joined target ids present in the segment
};

// Appends and reads index.csv living in the storage directory.
class Index {
public:
    explicit Index(std::string dir) : dir_(std::move(dir)) {}

    // Appends one entry, writing the CSV header first if the file is new.
    void append(const IndexEntry& e);

    // Loads all entries (skips the header row). Missing file -> empty.
    std::vector<IndexEntry> load() const;

    // Rebuilds index.csv by scanning every seg_*.zst header in the directory.
    // Returns the number of segments indexed.
    size_t rebuild();

    std::string path() const { return dir_ + "/index.csv"; }

private:
    std::string dir_;
};

}  // namespace pt
