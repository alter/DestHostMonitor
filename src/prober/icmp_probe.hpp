#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>

namespace pt {

// Resolves a hostname or literal to an IPv4 address (network byte order,
// i.e. an IPAddr usable directly by IcmpSendEcho2). Returns 0 on failure.
uint32_t resolve_ipv4(const std::string& host);

// Formats an IPv4 address (network byte order) as dotted decimal.
std::string ipv4_to_string(uint32_t addr);

// One reusable ICMP echo handle. Not thread-safe: use one per worker.
class IcmpProbe {
public:
    IcmpProbe();
    ~IcmpProbe();

    IcmpProbe(const IcmpProbe&) = delete;
    IcmpProbe& operator=(const IcmpProbe&) = delete;

    bool valid() const;

    // Sends one echo to dest (IPv4, network order) with the given TTL and blocks
    // up to timeout_ms. For a hop probe (TTL chosen so the packet expires at an
    // intermediate router), set hop_probe=true: a Time-Exceeded reply then counts
    // as "reachable" (Ok + RTT), and src_addr carries which router answered.
    ProbeResult ping(uint32_t dest, uint32_t timeout_ms, uint8_t ttl = 128,
                     bool hop_probe = false);

private:
    void* handle_;  // HANDLE from IcmpCreateFile (INVALID_HANDLE_VALUE if open failed)
};

}  // namespace pt
