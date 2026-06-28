#pragma once

#include "config.hpp"

#include <cstdint>
#include <string>

namespace pt {

// Decodes raw records in [from_utc, to_utc] from the sealed segments and writes
// them as CSV to `out_path` (or stdout if empty). `target_filter` matches a
// target name or numeric id; empty means all. Returns a process exit code.
int run_export(const Config& cfg, uint64_t from_utc, uint64_t to_utc,
               const std::string& target_filter, const std::string& out_path);

}  // namespace pt
