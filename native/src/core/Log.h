#pragma once

#include <string>
#include <string_view>

namespace xgu {

enum class LogLevel : int { Debug = 0, Info = 1, Warning = 2, Error = 3 };

using LogCallback = void (*)(void* user, int level, const char* message);

// Process-wide log sink. Thread-safe.
//
// Two delivery modes:
//   * direct  - write() calls the installed callback on the calling thread
//               (standalone host, CLI, tests, and the Unity plugin before the
//               managed side is up).
//   * queued  - write() appends to a bounded queue that the host drains on its
//               main thread with poll(). Unity uses this so log messages from
//               the runtime thread reach Debug.Log on the main thread.
//
// Without a callback and without queueing, messages go to OutputDebugString
// and stderr.
class Log {
public:
    static void setCallback(LogCallback fn, void* user);
    static void write(LogLevel level, std::string_view message);
    static void writef(LogLevel level, const char* fmt, ...);
    static const char* levelName(LogLevel level);

    // Switches between direct and queued delivery. Disabling flushes whatever is
    // still queued through the callback.
    static void setQueueEnabled(bool enabled);
    static bool queueEnabled();

    // Pops the oldest queued message. Returns false when the queue is empty.
    static bool poll(LogLevel& level, std::string& message);

    // Number of messages dropped because the queue was full (and resets it).
    static size_t takeDroppedCount();

    static constexpr size_t kMaxQueuedMessages = 4096;
};

} // namespace xgu

#define XGU_LOG_DEBUG(...) ::xgu::Log::writef(::xgu::LogLevel::Debug, __VA_ARGS__)
#define XGU_LOG_INFO(...) ::xgu::Log::writef(::xgu::LogLevel::Info, __VA_ARGS__)
#define XGU_LOG_WARNING(...) ::xgu::Log::writef(::xgu::LogLevel::Warning, __VA_ARGS__)
#define XGU_LOG_ERROR(...) ::xgu::Log::writef(::xgu::LogLevel::Error, __VA_ARGS__)
