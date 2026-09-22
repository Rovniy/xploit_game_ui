#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace xgu {

// The runtime thread executes everything that touches DOM/CSS/layout/JS: tasks
// posted from the host thread and one "tick" per host frame (coalesced).
//
// In inline mode (tests, CLI) there is no thread: post() and requestTick() run
// their work immediately on the calling thread.
class RuntimeThread {
public:
    using Task = std::function<void()>;
    using TickHandler = std::function<void(double timeSeconds)>;

    RuntimeThread() = default;
    ~RuntimeThread();

    RuntimeThread(const RuntimeThread&) = delete;
    RuntimeThread& operator=(const RuntimeThread&) = delete;

    void setTickHandler(TickHandler handler);

    // Starts the worker thread, or configures inline mode when `inlineMode` is true.
    void start(bool inlineMode);
    // Drains pending tasks and joins the thread. Idempotent.
    void stop();

    bool running() const { return running_.load(std::memory_order_acquire); }
    bool inlineMode() const { return inline_; }
    // True on the runtime thread (always true in inline mode).
    bool onRuntimeThread() const;

    // Any thread. Runs `task` on the runtime thread (immediately in inline mode).
    void post(Task task);
    // Any thread. Requests one tick with the given time; multiple requests before
    // the tick runs are coalesced (latest time wins).
    void requestTick(double timeSeconds);

private:
    void loop();

    TickHandler tickHandler_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    std::atomic<bool> running_{false};
    bool stopRequested_ = false;
    bool tickRequested_ = false;
    double tickTime_ = 0.0;
    bool inline_ = false;
    std::atomic<std::thread::id> threadId_{};
};

} // namespace xgu
