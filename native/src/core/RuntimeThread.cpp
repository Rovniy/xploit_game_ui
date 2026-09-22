#include "core/RuntimeThread.h"

#include "core/Log.h"

#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace xgu {

RuntimeThread::~RuntimeThread() { stop(); }

void RuntimeThread::setTickHandler(TickHandler handler) {
    std::lock_guard lock(mutex_);
    tickHandler_ = std::move(handler);
}

void RuntimeThread::start(bool inlineMode) {
    if (running()) {
        return;
    }
    inline_ = inlineMode;
    stopRequested_ = false;
    running_.store(true, std::memory_order_release);
    if (inline_) {
        threadId_.store(std::thread::id{});
        return;
    }
    thread_ = std::thread([this] { loop(); });
}

void RuntimeThread::stop() {
    if (!running()) {
        return;
    }
    if (inline_) {
        std::deque<Task> pending;
        {
            std::lock_guard lock(mutex_);
            pending.swap(tasks_);
        }
        for (auto& task : pending) {
            task();
        }
        running_.store(false, std::memory_order_release);
        return;
    }
    {
        std::lock_guard lock(mutex_);
        stopRequested_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_release);
}

bool RuntimeThread::onRuntimeThread() const {
    if (inline_) {
        return true;
    }
    return std::this_thread::get_id() == threadId_.load(std::memory_order_acquire);
}

void RuntimeThread::post(Task task) {
    if (!task) {
        return;
    }
    if (inline_) {
        task();
        return;
    }
    {
        std::lock_guard lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void RuntimeThread::requestTick(double timeSeconds) {
    if (inline_) {
        TickHandler handler;
        {
            std::lock_guard lock(mutex_);
            handler = tickHandler_;
        }
        if (handler) {
            handler(timeSeconds);
        }
        return;
    }
    {
        std::lock_guard lock(mutex_);
        tickRequested_ = true;
        tickTime_ = timeSeconds;
    }
    cv_.notify_one();
}

void RuntimeThread::loop() {
    threadId_.store(std::this_thread::get_id(), std::memory_order_release);
#ifdef _WIN32
    SetThreadDescription(GetCurrentThread(), L"xploit_game_ui runtime");
#endif
    for (;;) {
        std::deque<Task> tasks;
        bool runTick = false;
        double tickTime = 0.0;
        TickHandler handler;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stopRequested_ || !tasks_.empty() || tickRequested_; });
            tasks.swap(tasks_);
            runTick = tickRequested_;
            tickRequested_ = false;
            tickTime = tickTime_;
            handler = tickHandler_;
            if (stopRequested_ && tasks.empty()) {
                break;
            }
        }
        for (auto& task : tasks) {
            task();
        }
        if (runTick && handler) {
            handler(tickTime);
        }
    }
    threadId_.store(std::thread::id{}, std::memory_order_release);
}

} // namespace xgu
