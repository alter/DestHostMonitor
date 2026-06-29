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

        for (int probe = 0; probe < probes_per_hop; ++probe) {
            IP_OPTION_INFORMATION opts{};
            opts.Ttl = static_cast<UCHAR>(ttl);

            const DWORD n = IcmpSendEcho2(
                h, nullptr, nullptr, nullptr, static_cast<IPAddr>(dest),
                const_cast<char*>(kPayload), static_cast<WORD>(sizeof(kPayload)),
                &opts, reply.data(), kReplySize, timeout_ms);
            if (n == 0) continue;  // this probe timed out

            const auto* r = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply.data());
            const ULONG tenths = r->RoundTripTime * 10;
            const uint16_t rtt = (tenths >= kRttNa) ? (kRttNa - 1)
                                                    : static_cast<uint16_t>(tenths);
            if (r->Status == IP_SUCCESS) hop.reached = true;

            // Record this responder; keep the best RTT if seen before (ECMP can
            // make the same TTL answer from several addresses).
            bool found = false;
            for (auto& rep : hop.replies) {
                if (rep.addr == r->Address) {
                    if (rep.rtt_tenths == kRttNa || rtt < rep.rtt_tenths) rep.rtt_tenths = rtt;
                    found = true;
                    break;
                }
            }
            if (!found) hop.replies.push_back({r->Address, rtt});
        }

        hops.push_back(hop);
        if (hop.reached) break;
    }

    IcmpCloseHandle(h);
    return hops;
}

}  // namespace pt
