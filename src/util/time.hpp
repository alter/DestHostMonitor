#pragma once

#include <cstdint>
#include <string>

namespace pt {

// Current wall-clock time as Unix epoch milliseconds, UTC.
uint64_t now_utc_ms();

// Monotonic high-resolution counter in milliseconds (QueryPerformanceCounter).
// Unaffected by wall-clock adjustments; use for scheduling and RTT.
double mono_ms();

// Formats a UTC epoch-ms value as "YYYY-MM-DD HH:MM:SS.mmm" (UTC).
std::string format_utc_ms(uint64_t ms);

// Formats a UTC epoch-ms value as "YYYY-MM-DD" (UTC date only).
std::string format_utc_date(uint64_t ms);

// Formats a UTC epoch-ms value as local "YYYY-MM-DD HH:MM:SS" (DST-aware).
std::string format_local_ms(uint64_t ms);

// Local hour of day (0-23) for a UTC epoch-ms value, honouring DST.
int local_hour_of(uint64_t ms);

// Local day of week (0=Sunday .. 6=Saturday) for a UTC epoch-ms value.
int local_dow_of(uint64_t ms);

// Parses a time argument into UTC epoch ms. Accepts absolute local times
// "YYYY-MM-DD" or "YYYY-MM-DD HH:MM[:SS]" (interpreted in local time, DST-aware),
// relative offsets "<N>d" / "<N>h" / "<N>m" (that long ago), or "now".
// Returns false if unparseable. `now_ms` anchors relative offsets.
bool parse_time_arg(const std::string& s, uint64_t now_ms, uint64_t& out_ms);

}  // namespace pt
