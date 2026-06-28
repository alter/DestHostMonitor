#pragma once

#include "types.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace pt {

struct Hop {
    int      ttl        = 0;
    uint32_t addr       = 0;       // responder IPv4 (network order); 0 = no reply
    uint16_t rtt_tenths = kRttNa;
    bool     reached    = false;   // this hop is the destination
};

// Runs one TTL sweep toward `dest`, stopping when the destination replies or
// `max_hops` is reached. Single-shot, on demand only (plan §6). Each hop is
// probed `probes_per_hop` times; the first reply wins. If `stop` is provided,
// the sweep aborts between hops when it is set (keeps shutdown responsive).
std::vector<Hop> traceroute(uint32_t dest, int max_hops = 30,
                            uint32_t timeout_ms = 1000, int probes_per_hop = 2,
                            const std::atomic<bool>* stop = nullptr);

}  // namespace pt
