#pragma once

#include "storage/index.hpp"

#include <cstdint>
#include <string>

namespace pt {

// Seals a finished `.part` into seg_<start>.zst (zstd), appends an index entry,
// and removes the `.part`. If `end_utc` is 0 it is derived from the records.
// Returns false on any I/O / format failure (the `.part` is left intact).
bool seal_part(const std::string& dir, const std::string& part_path,
               uint64_t end_utc, Index& index);

// Seals every leftover `*.part` in `dir` (crash recovery). Returns count sealed.
size_t recover_dangling_parts(const std::string& dir, Index& index);

// Deletes sealed segments whose end time is older than `retention_days` and
// rewrites the index. Returns the number of files removed.
size_t enforce_retention(const std::string& dir, int retention_days,
                         uint64_t now_utc_ms, Index& index);

}  // namespace pt
