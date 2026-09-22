#pragma once

#include <v8.h>

#include <cstdint>
#include <vector>

namespace xgu::js {

class V8Runtime;

// setTimeout, setInterval, requestAnimationFrame and performance.now.
//
// Everything runs on the runtime thread from tick(), between the input for the
// frame and the restyle, which is where a browser fires them too. There is no
// separate timer thread: a game UI ticks with the game.
class Timers {
public:
    Timers(V8Runtime& runtime, v8::Isolate* isolate);
    ~Timers();

    // Installs the timer functions and `performance` on the context's global.
    void install(v8::Local<v8::Context> context);
    // Drops every pending callback.
    void dispose();

    // Fires whatever is due at `timeSeconds` (a monotonic clock from the host),
    // then every animation frame callback registered so far. Returns true when
    // anything ran, so the caller knows the document may have changed.
    bool tick(double timeSeconds);

    // Seconds since the first tick; what performance.now() reports in ms.
    double elapsedSeconds() const { return lastTime_ - startTime_; }

    size_t pendingTimers() const { return timers_.size(); }
    size_t pendingAnimationFrames() const { return animationFrames_.size(); }

private:
    struct Timer {
        uint64_t id = 0;
        double dueSeconds = 0.0;
        double intervalSeconds = 0.0;
        bool repeating = false;
        bool cancelled = false;
        v8::Global<v8::Function> callback;
        std::vector<v8::Global<v8::Value>> args;
    };

    struct AnimationFrame {
        uint64_t id = 0;
        bool cancelled = false;
        v8::Global<v8::Function> callback;
    };

    static void setTimeoutCallback(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void setIntervalCallback(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void clearTimerCallback(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void requestAnimationFrameCallback(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void cancelAnimationFrameCallback(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void performanceNowCallback(const v8::FunctionCallbackInfo<v8::Value>& info);

    uint64_t addTimer(const v8::FunctionCallbackInfo<v8::Value>& info, bool repeating);
    void cancel(uint64_t id);

    V8Runtime& runtime_;
    v8::Isolate* isolate_;
    std::vector<Timer> timers_;
    std::vector<AnimationFrame> animationFrames_;
    uint64_t nextId_ = 1;
    double startTime_ = -1.0;
    double lastTime_ = 0.0;
};

} // namespace xgu::js
