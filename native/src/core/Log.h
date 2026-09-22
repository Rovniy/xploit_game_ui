#pragma once

#include <string_view>

namespace xgu {

enum class LogLevel : int { Debug = 0, Info = 1, Warning = 2, Error = 3 };

using LogCallback = void (*)(void* user, int level, const char* message);

// Process-wide log sink. Thread-safe. Falls back to OutputDebugString + stderr
// when no callback is installed.
class Log {
public:
    static void setCallback(LogCallback fn, void* user);
    static void write(LogLevel level, std::string_view message);
    static void writef(LogLevel level, const char* fmt, ...);
    static const char* levelName(LogLevel level);
};

} // namespace xgu

#define XGU_LOG_DEBUG(...) ::xgu::Log::writef(::xgu::LogLevel::Debug, __VA_ARGS__)
#define XGU_LOG_INFO(...) ::xgu::Log::writef(::xgu::LogLevel::Info, __VA_ARGS__)
#define XGU_LOG_WARNING(...) ::xgu::Log::writef(::xgu::LogLevel::Warning, __VA_ARGS__)
#define XGU_LOG_ERROR(...) ::xgu::Log::writef(::xgu::LogLevel::Error, __VA_ARGS__)
