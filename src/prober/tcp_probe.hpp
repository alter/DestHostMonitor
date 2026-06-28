#pragma once

#include "types.hpp"

#include <cstdint>

namespace pt {

// One non-blocking TCP connect probe to (addr, port), measuring time to the
// SYN-ACK (port open) or RST (port closed) — both mean the host responded, so
// both yield Status::Ok with an RTT. A connect timeout yields Status::Timeout.
// Used to bypass ICMP deprioritisation on routers (plan §10).
ProbeResult tcp_ping(uint32_t addr, uint16_t port, uint32_t timeout_ms);

}  // namespace pt
