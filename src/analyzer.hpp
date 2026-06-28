#pragma once

#include "config.hpp"

#include <cstdint>
#include <string>

namespace pt {

struct AnalyzeOptions {
    uint64_t    from_utc       = 0;
    uint64_t    to_utc         = 0;
    std::string target_filter;        // name or id; empty = all
    bool        by_hour        = false;
    bool        heatmap        = false;
};

// Offline report over [from,to]: per-target summary and outage list always;
// loss-by-hour-of-day histogram with --by-hour; day-of-week x hour heatmap with
// --heatmap. Reads rollups and events.csv (kept forever). Returns an exit code.
int run_analyze(const Config& cfg, const AnalyzeOptions& opt);

}  // namespace pt
