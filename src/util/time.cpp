#include "util/time.hpp"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace pt {

uint64_t now_utc_ms() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    // FILETIME = 100 ns ticks since 1601-01-01.
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    constexpr uint64_t kTicksPerMs = 10000ULL;
    constexpr uint64_t kEpochDiffMs = 11644473600000ULL;  // 1601 -> 1970
    return u.QuadPart / kTicksPerMs - kEpochDiffMs;
}

double mono_ms() {
    static const double inv_freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1000.0 / static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * inv_freq;
}

std::string format_utc_ms(uint64_t ms) {
    const std::time_t secs = static_cast<std::time_t>(ms / 1000);
    const unsigned millis = static_cast<unsigned>(ms % 1000);
    std::tm tm{};
    gmtime_s(&tm, &secs);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03u",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, millis);
    return buf;
}

std::string format_utc_date(uint64_t ms) {
    const std::time_t secs = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    gmtime_s(&tm, &secs);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

std::string format_local_ms(uint64_t ms) {
    const std::time_t secs = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    localtime_s(&tm, &secs);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

int local_hour_of(uint64_t ms) {
    const std::time_t secs = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    localtime_s(&tm, &secs);
    return tm.tm_hour;
}

int local_dow_of(uint64_t ms) {
    const std::time_t secs = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    localtime_s(&tm, &secs);
    return tm.tm_wday;
}

bool parse_time_arg(const std::string& s, uint64_t now_ms, uint64_t& out_ms) {
    if (s.empty()) return false;
    if (s == "now") { out_ms = now_ms; return true; }

    // Relative: "<N>d" / "<N>h" / "<N>m".
    const char unit = static_cast<char>(std::tolower(s.back()));
    if ((unit == 'd' || unit == 'h' || unit == 'm') &&
        s.find_first_not_of("0123456789") == s.size() - 1) {
        const long long n = std::atoll(s.c_str());
        uint64_t ms = 0;
        if (unit == 'd') ms = static_cast<uint64_t>(n) * 86400000ULL;
        else if (unit == 'h') ms = static_cast<uint64_t>(n) * 3600000ULL;
        else ms = static_cast<uint64_t>(n) * 60000ULL;
        out_ms = (now_ms > ms) ? now_ms - ms : 0;
        return true;
    }

    // Absolute local: "YYYY-MM-DD[ HH:MM[:SS]]".
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    const int got = std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se);
    if (got < 3) return false;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon  = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min  = mi;
    tm.tm_sec  = se;
    tm.tm_isdst = -1;  // let the CRT resolve DST for this local time
    const std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return false;
    out_ms = static_cast<uint64_t>(t) * 1000ULL;
    return true;
}

}  // namespace pt
