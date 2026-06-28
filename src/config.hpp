#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pt {

struct StorageConfig {
    std::string dir                = "data";
    int         segment_minutes    = 60;
    int         raw_retention_days = 14;
};

struct EventsConfig {
    int  fail_threshold      = 3;
    int  min_outage_ms       = 2000;
    bool trigger_traceroute  = true;
};

struct Config {
    StorageConfig            storage;
    uint32_t                 default_interval_ms = 1000;
    uint32_t                 default_timeout_ms  = 1500;
    EventsConfig             events;
    std::vector<Target>      targets;
};

// Loads and validates a config file. Throws std::runtime_error on any problem
// (missing file, bad JSON, no targets). Target ids are assigned sequentially.
Config load_config(const std::string& path);

}  // namespace pt
