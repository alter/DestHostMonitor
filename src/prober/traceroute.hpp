#pragma once

#include "types.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace pt {

// One responder seen at a given TTL.
struct HopReply {
    uint32_t addr       = 0;
    uint16_t rtt_tenths = kRttNa;
};

// One hop = all distinct responders seen across the probes at this TTL. With
// ECMP a hop can answer from several addresses; reporting all of them exposes
// the multipath honestly (the no-admin alternative to Paris traceroute, which
// would need raw sockets to pin a single path).
struct Hop {
    int                   ttl     = 0;
    std::vector<HopReply> replies;     // empty = no reply (timeout)
    bool                  reached = false;  // the destination answered
};

// Runs one TTL sweep toward `dest`, sending `probes_per_hop` probes per hop and
// collecting every distinct responder. Stops when the destination replies or
// `max_hops` is reached. If `stop` is set the sweep aborts between hops.
std::vector<Hop> traceroute(uint32_t dest, int max_hops = 30,
                            uint32_t timeout_ms = 1000, int probes_per_hop = 4,
                            const std::atomic<bool>* stop = nullptr);

}  // namespace pt
