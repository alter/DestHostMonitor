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

    // Sends one echo to dest (IPv4, network order) and blocks up to timeout_ms.
    ProbeResult ping(uint32_t dest, uint32_t timeout_ms);

private:
    void* handle_;  // HANDLE from IcmpCreateFile (INVALID_HANDLE_VALUE if open failed)
};

}  // namespace pt
