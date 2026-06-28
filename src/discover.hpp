#pragma once

#include <string>

namespace pt {

// Runs one traceroute to `dest`, prints the hop ladder, and (when `add` is set)
// appends the responding intermediate hops to `config_path` as ICMP targets
// tagged with path_group=<dest> and hop_index. Returns a process exit code.
int run_discover(const std::string& dest, bool add, const std::string& config_path);

}  // namespace pt
