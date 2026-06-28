#include "util/log.hpp"

#include "util/time.hpp"

#include <cstdio>

namespace pt {

namespace {
const char* level_tag(LogLevel l) {
    switch (l) {
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}
}  // namespace

void log_msg(LogLevel level, std::string_view msg) {
    std::fprintf(stderr, "[%s] %-5s %.*s\n",
                 format_utc_ms(now_utc_ms()).c_str(),
                 level_tag(level),
                 static_cast<int>(msg.size()), msg.data());
}

}  // namespace pt
