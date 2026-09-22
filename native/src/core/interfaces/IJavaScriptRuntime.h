#pragma once

#include <functional>
#include <memory>
#include <string_view>

namespace xgu {

class View;

// Per-view JavaScript engine. All methods run on the runtime thread (the same
// thread that owns the view's DOM); the default implementation is js::V8Runtime
// with one V8 isolate per view.
class IJavaScriptRuntime {
public:
    virtual ~IJavaScriptRuntime() = default;

    // Creates the engine instance (isolate, global object). Returns false when
    // the engine is unavailable; the view then runs without scripting.
    virtual bool initialize() = 0;
    virtual bool ready() const = 0;

    // Compiles and runs a classic script. Errors are reported through the log
    // sink as "Uncaught ..." with a stack trace; they never propagate.
    virtual void evaluate(std::string_view source, std::string_view origin) = 0;

    // Once per frame: pumps the engine's message loop and microtasks (timers and
    // requestAnimationFrame are layered on top in later stages).
    virtual void tick(double timeSeconds) = 0;

    // Releases every engine object. Safe to call twice.
    virtual void dispose() = 0;
};

using JavaScriptRuntimeFactory = std::function<std::unique_ptr<IJavaScriptRuntime>(View&)>;

} // namespace xgu
