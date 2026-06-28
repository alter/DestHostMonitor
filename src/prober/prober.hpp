#pragma once

#include "config.hpp"

#include <atomic>

namespace pt {

// Runs the continuous probing loop: scheduler -> worker pool -> writer thread,
// appending raw records to a segment. Blocks until *stop is set. Returns a
// process exit code. `simulate_gap_ms` > 0 injects one synthetic monitor gap a
// few seconds in (testing the MONITOR_GAP path without a real sleep).
int run_prober(const Config& cfg, std::atomic<bool>* stop, uint32_t simulate_gap_ms = 0);

}  // namespace pt
