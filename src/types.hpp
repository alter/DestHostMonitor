#pragma once

#include <cstdint>
#include <string>

namespace pt {

// Probe outcome status. Stored as u8 in the raw record (see plan §4).
enum class Status : uint8_t {
    Ok           = 0,  // echo reply received
    Timeout      = 1,  // no reply within timeout
    DestUnreach  = 2,  // ICMP destination unreachable
    TtlExpired   = 3,  // ICMP time exceeded in transit
    OtherIcmpErr = 4,  // any other ICMP error status
    SendErr      = 5,  // local send failure (no packet left the host)
    MonitorGap   = 6,  // synthetic: returned from sleep / clock jump
};

const char* to_string(Status s);

enum class Proto : uint8_t { Icmp, Tcp };

// A configured destination to probe.
struct Target {
    uint16_t    id          = 0;
    std::string name;
    std::string address;          // hostname or literal IP as configured
    Proto       proto       = Proto::Icmp;
    uint16_t    port        = 0;  // TCP only
    uint32_t    interval_ms = 1000;
    uint32_t    timeout_ms  = 1500;
    std::string path_group;       // ladder grouping for localization
    int         hop_index   = -1; // position in the ladder, -1 = n/a
};

constexpr uint16_t kRttNa = 0xFFFF;  // sentinel for "RTT not available"

// Outcome of a single probe attempt.
struct ProbeResult {
    Status   status        = Status::Timeout;
    uint8_t  reply_ttl     = 0;       // TTL of the reply, 0 = n/a
    uint16_t rtt_tenths_ms = kRttNa;  // RTT in 0.1 ms units, kRttNa = n/a
    uint32_t src_addr      = 0;       // IPv4 (network order) of ICMP responder; 0 = n/a
};

// A completed probe handed from a worker to the writer thread. A synthetic
// sample with result.status == MonitorGap carries the gap length in `gap_ms`
// and is emitted by the scheduler when the monitor slept or the clock jumped.
struct ProbeSample {
    uint16_t    target_id = 0;
    uint64_t    utc_ms    = 0;  // wall-clock time the probe completed
    ProbeResult result;
    uint32_t    gap_ms    = 0;  // MonitorGap only: length of the detected gap
};

// The fixed 12-byte on-disk raw record (plan §4). `src_id` indexes the segment
// address table; 0 = the target itself / n/a.
struct RawRecord {
    uint16_t target_id     = 0;
    uint32_t t_offset_ms   = 0;  // ms since the segment start
    uint8_t  status        = 0;
    uint8_t  reply_ttl     = 0;
    uint16_t rtt_tenths_ms = kRttNa;
    uint16_t src_id        = 0;
};

constexpr size_t kRawRecordSize = 12;

}  // namespace pt
