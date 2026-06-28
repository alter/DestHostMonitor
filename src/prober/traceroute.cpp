#include "prober/traceroute.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <array>

namespace pt {

std::vector<Hop> traceroute(uint32_t dest, int max_hops, uint32_t timeout_ms,
                            int probes_per_hop, const std::atomic<bool>* stop) {
    std::vector<Hop> hops;
    const HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) return hops;

    static constexpr char kPayload[] = "pingtrace-trace";
    constexpr DWORD kReplySize = sizeof(ICMP_ECHO_REPLY) + sizeof(kPayload) + 16;
    std::array<unsigned char, kReplySize> reply{};

    for (int ttl = 1; ttl <= max_hops; ++ttl) {
        if (stop && stop->load()) break;  // abandon on shutdown
        Hop hop;
        hop.ttl = ttl;

        for (int probe = 0; probe < probes_per_hop && hop.addr == 0 && !hop.reached; ++probe) {
            IP_OPTION_INFORMATION opts{};
            opts.Ttl = static_cast<UCHAR>(ttl);

            const DWORD n = IcmpSendEcho2(
                h, nullptr, nullptr, nullptr, static_cast<IPAddr>(dest),
                const_cast<char*>(kPayload), static_cast<WORD>(sizeof(kPayload)),
                &opts, reply.data(), kReplySize, timeout_ms);
            if (n == 0) continue;  // no reply at this TTL for this probe

            const auto* r = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply.data());
            hop.addr = r->Address;
            const ULONG tenths = r->RoundTripTime * 10;
            hop.rtt_tenths = (tenths >= kRttNa) ? (kRttNa - 1) : static_cast<uint16_t>(tenths);
            if (r->Status == IP_SUCCESS) hop.reached = true;
        }

        hops.push_back(hop);
        if (hop.reached) break;
    }

    IcmpCloseHandle(h);
    return hops;
}

}  // namespace pt
