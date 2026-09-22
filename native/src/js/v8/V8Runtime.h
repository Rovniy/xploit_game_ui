#pragma once

#include "core/interfaces/IJavaScriptRuntime.h"

#include <v8.h>

#include <memory>
#include <string>

namespace xgu {
class View;
}

namespace xgu::js {

// One V8 isolate + context per view. Runtime thread only.
class V8Runtime final : public IJavaScriptRuntime {
public:
    explicit V8Runtime(View& view);
    ~V8Runtime() override;

    bool initialize() override;
    bool ready() const override { return isolate_ != nullptr; }
    void evaluate(std::string_view source, std::string_view origin) override;
    void tick(double timeSeconds) override;
    void dispose() override;

    v8::Isolate* isolate() const { return isolate_; }

    // Formats a JS value the way console.log does (strings raw, errors with stack,
    // objects as JSON, everything else via ToDetailString).
    std::string formatValue(v8::Local<v8::Context> context, v8::Local<v8::Value> value);

    static std::unique_ptr<IJavaScriptRuntime> create(View& view);

private:
    void installGlobals(v8::Local<v8::Context> context);
    void reportException(v8::TryCatch& tryCatch, v8::Local<v8::Context> context);
    static void consoleCallback(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void promiseRejectCallback(v8::PromiseRejectMessage message);

    View& view_;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator_;
    v8::Isolate* isolate_ = nullptr;
    v8::Global<v8::Context> context_;
};

} // namespace xgu::js
