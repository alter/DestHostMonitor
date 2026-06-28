#include "prober/tcp_probe.hpp"

#include "util/time.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace pt {

ProbeResult tcp_ping(uint32_t addr, uint16_t port, uint32_t timeout_ms) {
    ProbeResult out;

    const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        out.status = Status::SendErr;
        return out;
    }

    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = addr;          // network order
    sa.sin_port = htons(port);

    const double t0 = mono_ms();
    const int rc = ::connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    if (rc == 0) {
        // Immediate connect (rare on non-blocking) — treat as reachable.
        out.status = Status::Ok;
        out.rtt_tenths_ms = 0;
        ::closesocket(s);
        return out;
    }

    if (WSAGetLastError() != WSAEWOULDBLOCK) {
        out.status = Status::SendErr;
        ::closesocket(s);
        return out;
    }

    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_SET(s, &wfds);
    FD_ZERO(&efds); FD_SET(s, &efds);
    timeval tv;
    tv.tv_sec  = static_cast<long>(timeout_ms / 1000);
    tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);

    const int sel = ::select(0, nullptr, &wfds, &efds, &tv);
    const double rtt_ms = mono_ms() - t0;

    if (sel == 0) {
        out.status = Status::Timeout;        // no response within timeout
    } else if (sel == SOCKET_ERROR) {
        out.status = Status::SendErr;
    } else {
        int err = 0;
        int len = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
        if (err == 0 || err == WSAECONNREFUSED) {
            // SYN-ACK (open) or RST (closed): the host is reachable.
            out.status = Status::Ok;
            const double tenths = rtt_ms * 10.0;
            out.rtt_tenths_ms = (tenths >= kRttNa) ? (kRttNa - 1)
                                                   : static_cast<uint16_t>(tenths + 0.5);
        } else if (err == WSAEHOSTUNREACH || err == WSAENETUNREACH) {
            out.status = Status::DestUnreach;
        } else {
            out.status = Status::Timeout;
        }
    }

    ::closesocket(s);
    return out;
}

}  // namespace pt
