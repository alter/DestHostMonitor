#include "prober/icmp_probe.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <array>
#include <cstdio>

namespace pt {

uint32_t resolve_ipv4(const std::string& host) {
    addrinfo hints{};
    hints.ai_family = AF_INET;       // IPv4 only for ICMP echo here
    hints.ai_socktype = SOCK_RAW;

    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
        return 0;
    }
    uint32_t addr = 0;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        if (p->ai_family == AF_INET) {
            auto* sin = reinterpret_cast<sockaddr_in*>(p->ai_addr);
            addr = sin->sin_addr.s_addr;  // network byte order == IPAddr
            break;
        }
    }
    freeaddrinfo(res);
    return addr;
}

std::string ipv4_to_string(uint32_t addr) {
    in_addr a{};
    a.s_addr = addr;
    char buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

namespace {

// Maps a Win32 IP_STATUS reply code to our Status enum.
Status map_ip_status(ULONG ip_status) {
    switch (ip_status) {
        case IP_SUCCESS:
            return Status::Ok;
        case IP_REQ_TIMED_OUT:
            return Status::Timeout;
        case IP_DEST_HOST_UNREACHABLE:
        case IP_DEST_NET_UNREACHABLE:
        case IP_DEST_PROT_UNREACHABLE:
        case IP_DEST_PORT_UNREACHABLE:
        case IP_DEST_UNREACHABLE:
        case IP_BAD_DESTINATION:
            return Status::DestUnreach;
        case IP_TTL_EXPIRED_TRANSIT:
        case IP_TTL_EXPIRED_REASSEM:
            return Status::TtlExpired;
        default:
            return Status::OtherIcmpErr;
    }
}

}  // namespace

IcmpProbe::IcmpProbe() : handle_(IcmpCreateFile()) {}

IcmpProbe::~IcmpProbe() {
    if (valid()) {
        IcmpCloseHandle(handle_);
    }
}

bool IcmpProbe::valid() const {
    return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
}

ProbeResult IcmpProbe::ping(uint32_t dest, uint32_t timeout_ms, uint8_t ttl, bool hop_probe) {
    ProbeResult out;
    if (!valid()) {
        out.status = Status::SendErr;
        return out;
    }

    // Small fixed payload; reply buffer must hold the reply struct + data + ICMP error.
    static constexpr char kPayload[] = "pingtrace";
    constexpr DWORD kReplySize =
        sizeof(ICMP_ECHO_REPLY) + sizeof(kPayload) + 8 /*ICMP error*/ + 8;
    std::array<unsigned char, kReplySize> reply{};

    IP_OPTION_INFORMATION opts{};
    opts.Ttl = ttl ? ttl : 128;

    const DWORD n = IcmpSendEcho2(
        handle_, /*Event*/ nullptr, /*ApcRoutine*/ nullptr, /*ApcContext*/ nullptr,
        static_cast<IPAddr>(dest),
        const_cast<char*>(kPayload), static_cast<WORD>(sizeof(kPayload)),
        &opts, reply.data(), kReplySize, timeout_ms);

    if (n == 0) {
        const DWORD err = GetLastError();
        // IP_REQ_TIMED_OUT surfaces here as a last-error when no reply structure
        // is produced; treat it as a timeout rather than a local send error.
        out.status = (err == IP_REQ_TIMED_OUT) ? Status::Timeout : Status::SendErr;
        return out;
    }

    const auto* r = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply.data());
    const auto rtt_tenths = [&] {
        const ULONG tenths = r->RoundTripTime * 10;
        return (tenths >= kRttNa) ? static_cast<uint16_t>(kRttNa - 1)
                                  : static_cast<uint16_t>(tenths);
    };

    if (hop_probe) {
        // A hop probe wants the router at this TTL to reply Time-Exceeded; that
        // (or actually reaching the aim) means the hop is reachable.
        out.reply_ttl = r->Options.Ttl;
        out.src_addr  = r->Address;  // which router answered (route change visible)
        if (r->Status == IP_TTL_EXPIRED_TRANSIT || r->Status == IP_TTL_EXPIRED_REASSEM ||
            r->Status == IP_SUCCESS) {
            out.status = Status::Ok;
            out.rtt_tenths_ms = rtt_tenths();
        } else {
            out.status = map_ip_status(r->Status);  // unreachable etc. = real problem
        }
        return out;
    }

    out.status = map_ip_status(r->Status);
    out.reply_ttl = r->Options.Ttl;
    out.src_addr = r->Address;  // responder address (target on success, router on error)

    if (out.status == Status::Ok) {
        out.rtt_tenths_ms = rtt_tenths();
        out.src_addr = 0;  // success: source is the target itself, not a router
    }
    return out;
}

}  // namespace pt
