#include "core/Log.h"

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace xgu {
namespace {

struct QueuedMessage {
    LogLevel level;
    std::string text;
};

std::mutex g_mutex;
LogCallback g_callback = nullptr;
void* g_user = nullptr;
bool g_queueEnabled = false;
std::deque<QueuedMessage> g_queue;
size_t g_dropped = 0;

void emitDirect(LogLevel level, const std::string& text, LogCallback fn, void* user) {
    if (fn) {
        fn(user, static_cast<int>(level), text.c_str());
        return;
    }
    const std::string line = text + "\n";
#ifdef _WIN32
    OutputDebugStringA(line.c_str());
#endif
    std::fputs(line.c_str(), stderr);
}

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
    std::string text;
    text.reserve(message.size() + 24);
    text.append("[xploit_game_ui] ");
    text.append(message);

    LogCallback fn = nullptr;
    void* user = nullptr;
    {
        std::lock_guard lock(g_mutex);
        if (g_queueEnabled) {
            if (g_queue.size() >= kMaxQueuedMessages) {
                g_queue.pop_front();
                ++g_dropped;
            }
            g_queue.push_back(QueuedMessage{level, std::move(text)});
            return;
        }
        fn = g_callback;
        user = g_user;
    }
    emitDirect(level, text, fn, user);
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

void Log::setQueueEnabled(bool enabled) {
    std::deque<QueuedMessage> flush;
    LogCallback fn = nullptr;
    void* user = nullptr;
    {
        std::lock_guard lock(g_mutex);
        if (g_queueEnabled == enabled) {
            return;
        }
        g_queueEnabled = enabled;
        if (!enabled) {
            flush.swap(g_queue);
            fn = g_callback;
            user = g_user;
        }
    }
    for (const QueuedMessage& message : flush) {
        emitDirect(message.level, message.text, fn, user);
    }
}

bool Log::queueEnabled() {
    std::lock_guard lock(g_mutex);
    return g_queueEnabled;
}

bool Log::poll(LogLevel& level, std::string& message) {
    std::lock_guard lock(g_mutex);
    if (g_queue.empty()) {
        return false;
    }
    level = g_queue.front().level;
    message = std::move(g_queue.front().text);
    g_queue.pop_front();
    return true;
}

size_t Log::takeDroppedCount() {
    std::lock_guard lock(g_mutex);
    return std::exchange(g_dropped, size_t{0});
}

} // namespace xgu
