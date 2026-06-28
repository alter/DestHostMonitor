#pragma once

#include <string>
#include <string_view>

namespace pt {

// Internal program log (diagnostics, NOT measurement data). Writes to stderr.
enum class LogLevel { Info, Warn, Error };

void log_msg(LogLevel level, std::string_view msg);

inline void log_info(std::string_view m)  { log_msg(LogLevel::Info, m); }
inline void log_warn(std::string_view m)  { log_msg(LogLevel::Warn, m); }
inline void log_error(std::string_view m) { log_msg(LogLevel::Error, m); }

}  // namespace pt
