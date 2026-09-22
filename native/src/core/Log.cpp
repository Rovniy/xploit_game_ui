#include "core/Log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace xgu {
namespace {

std::mutex g_mutex;
LogCallback g_callback = nullptr;
void* g_user = nullptr;

} // namespace

void Log::setCallback(LogCallback fn, void* user) {
    std::lock_guard lock(g_mutex);
    g_callback = fn;
    g_user = user;
}

const char* Log::levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "?";
}

void Log::write(LogLevel level, std::string_view message) {
    LogCallback fn;
    void* user;
    {
        std::lock_guard lock(g_mutex);
        fn = g_callback;
        user = g_user;
    }
    std::string text;
    text.reserve(message.size() + 24);
    text.append("[xploit_game_ui] ");
    text.append(message);
    if (fn) {
        fn(user, static_cast<int>(level), text.c_str());
        return;
    }
    text.push_back('\n');
#ifdef _WIN32
    OutputDebugStringA(text.c_str());
#endif
    std::fputs(text.c_str(), stderr);
}

void Log::writef(LogLevel level, const char* fmt, ...) {
    char stackBuffer[1024];
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = std::vsnprintf(stackBuffer, sizeof(stackBuffer), fmt, args);
    va_end(args);
    if (needed < 0) {
        va_end(copy);
        write(level, fmt);
        return;
    }
    if (static_cast<size_t>(needed) < sizeof(stackBuffer)) {
        va_end(copy);
        write(level, std::string_view(stackBuffer, static_cast<size_t>(needed)));
        return;
    }
    std::string heap(static_cast<size_t>(needed) + 1, '\0');
    std::vsnprintf(heap.data(), heap.size(), fmt, copy);
    va_end(copy);
    heap.resize(static_cast<size_t>(needed));
    write(level, heap);
}

} // namespace xgu
