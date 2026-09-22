#include "js/v8/Timers.h"

#include "js/v8/V8Runtime.h"

#include <algorithm>

namespace xgu::js {
namespace {

v8::Local<v8::String> toV8(v8::Isolate* isolate, const char* text) {
    return v8::String::NewFromUtf8(isolate, text).FromMaybe(v8::String::Empty(isolate));
}

Timers* timersOf(v8::Isolate* isolate) {
    V8Runtime* runtime = V8Runtime::fromIsolate(isolate);
    return runtime ? runtime->timers() : nullptr;
}

} // namespace

Timers::Timers(V8Runtime& runtime, v8::Isolate* isolate) : runtime_(runtime), isolate_(isolate) {}

Timers::~Timers() { dispose(); }

void Timers::install(v8::Local<v8::Context> context) {
    v8::HandleScope scope(isolate_);
    v8::Local<v8::Object> global = context->Global();

    const auto method = [&](v8::Local<v8::Object> target, const char* name, v8::FunctionCallback callback) {
        v8::Local<v8::Function> function;
        if (v8::FunctionTemplate::New(isolate_, callback)->GetFunction(context).ToLocal(&function)) {
            target->Set(context, toV8(isolate_, name), function).Check();
        }
    };

    method(global, "setTimeout", setTimeoutCallback);
    method(global, "setInterval", setIntervalCallback);
    // clearTimeout and clearInterval are the same operation on the same ids.
    method(global, "clearTimeout", clearTimerCallback);
    method(global, "clearInterval", clearTimerCallback);
    method(global, "requestAnimationFrame", requestAnimationFrameCallback);
    method(global, "cancelAnimationFrame", cancelAnimationFrameCallback);

    v8::Local<v8::Object> performance = v8::Object::New(isolate_);
    method(performance, "now", performanceNowCallback);
    global->Set(context, toV8(isolate_, "performance"), performance).Check();
}

void Timers::dispose() {
    timers_.clear();
    animationFrames_.clear();
}

uint64_t Timers::addTimer(const v8::FunctionCallbackInfo<v8::Value>& info, bool repeating) {
    if (info.Length() < 1 || !info[0]->IsFunction()) {
        return 0; // a string body would need eval, which the runtime does not have
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    double delayMs = 0.0;
    if (info.Length() > 1) {
        delayMs = info[1]->NumberValue(context).FromMaybe(0.0);
    }
    if (!(delayMs >= 0.0)) {
        delayMs = 0.0; // NaN and negatives mean "as soon as possible"
    }
    // A repeating timer with no delay would run flat out; browsers clamp it too.
    const double intervalSeconds = std::max(repeating ? 0.004 : 0.0, delayMs / 1000.0);

    Timer timer;
    timer.id = nextId_++;
    timer.dueSeconds = lastTime_ + intervalSeconds;
    timer.intervalSeconds = intervalSeconds;
    timer.repeating = repeating;
    timer.callback.Reset(isolate, info[0].As<v8::Function>());
    for (int i = 2; i < info.Length(); ++i) {
        timer.args.emplace_back(isolate, info[i]);
    }
    timers_.push_back(std::move(timer));
    return timers_.back().id;
}

void Timers::cancel(uint64_t id) {
    for (Timer& timer : timers_) {
        if (timer.id == id) {
            timer.cancelled = true;
            return;
        }
    }
    for (AnimationFrame& frame : animationFrames_) {
        if (frame.id == id) {
            frame.cancelled = true;
            return;
        }
    }
}

void Timers::setTimeoutCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (Timers* timers = timersOf(info.GetIsolate())) {
        info.GetReturnValue().Set(static_cast<double>(timers->addTimer(info, false)));
    }
}

void Timers::setIntervalCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (Timers* timers = timersOf(info.GetIsolate())) {
        info.GetReturnValue().Set(static_cast<double>(timers->addTimer(info, true)));
    }
}

void Timers::clearTimerCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    Timers* timers = timersOf(info.GetIsolate());
    if (!timers || info.Length() < 1) {
        return;
    }
    const double id = info[0]->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(0.0);
    if (id > 0.0) {
        timers->cancel(static_cast<uint64_t>(id));
    }
}

void Timers::requestAnimationFrameCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    Timers* timers = timersOf(info.GetIsolate());
    if (!timers || info.Length() < 1 || !info[0]->IsFunction()) {
        return;
    }
    AnimationFrame frame;
    frame.id = timers->nextId_++;
    frame.callback.Reset(info.GetIsolate(), info[0].As<v8::Function>());
    timers->animationFrames_.push_back(std::move(frame));
    info.GetReturnValue().Set(static_cast<double>(timers->animationFrames_.back().id));
}

void Timers::cancelAnimationFrameCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    clearTimerCallback(info);
}

void Timers::performanceNowCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (Timers* timers = timersOf(info.GetIsolate())) {
        info.GetReturnValue().Set(timers->elapsedSeconds() * 1000.0);
    }
}

bool Timers::tick(double timeSeconds) {
    if (startTime_ < 0.0) {
        startTime_ = timeSeconds;
    }
    lastTime_ = timeSeconds;
    if (timers_.empty() && animationFrames_.empty()) {
        return false;
    }

    v8::Isolate::Scope isolateScope(isolate_);
    v8::HandleScope scope(isolate_);
    v8::Local<v8::Context> context = runtime_.context();
    if (context.IsEmpty()) {
        return false;
    }
    v8::Context::Scope contextScope(context);
    bool ranAnything = false;

    // Snapshot the ids that are due: a callback may add or cancel timers, and
    // the ones it adds must wait for the next frame, as in a browser.
    std::vector<uint64_t> due;
    for (const Timer& timer : timers_) {
        if (!timer.cancelled && timer.dueSeconds <= timeSeconds) {
            due.push_back(timer.id);
        }
    }
    for (const uint64_t id : due) {
        const auto it = std::find_if(timers_.begin(), timers_.end(),
                                     [id](const Timer& timer) { return timer.id == id; });
        if (it == timers_.end() || it->cancelled) {
            continue;
        }
        v8::Local<v8::Function> callback = it->callback.Get(isolate_);
        std::vector<v8::Local<v8::Value>> args;
        args.reserve(it->args.size());
        for (const v8::Global<v8::Value>& arg : it->args) {
            args.push_back(arg.Get(isolate_));
        }
        if (it->repeating) {
            // Schedule the next run before the callback, so cancelling from
            // inside it still wins.
            it->dueSeconds = timeSeconds + it->intervalSeconds;
        } else {
            it->cancelled = true;
        }
        ranAnything = true;
        runtime_.callFunction(callback, context->Global(), static_cast<int>(args.size()),
                              args.empty() ? nullptr : args.data());
    }
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [](const Timer& t) { return t.cancelled; }),
                  timers_.end());

    // Animation frames registered before this tick all run now; a callback that
    // asks for another frame is served next tick.
    if (!animationFrames_.empty()) {
        std::vector<AnimationFrame> frames;
        frames.swap(animationFrames_);
        const double nowMs = elapsedSeconds() * 1000.0;
        for (AnimationFrame& frame : frames) {
            if (frame.cancelled) {
                continue;
            }
            v8::Local<v8::Value> args[1] = {v8::Number::New(isolate_, nowMs)};
            ranAnything = true;
            runtime_.callFunction(frame.callback.Get(isolate_), context->Global(), 1, args);
        }
    }
    return ranAnything;
}

} // namespace xgu::js
