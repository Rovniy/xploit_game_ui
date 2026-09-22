#include "js/v8/V8Runtime.h"

#include "core/Log.h"
#include "core/View.h"
#include "js/v8/V8Platform.h"

#include <libplatform/libplatform.h>

#include <string>

namespace xgu::js {
namespace {

constexpr int kIsolateDataSlot = 0;

std::string toUtf8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
    if (value.IsEmpty()) {
        return {};
    }
    v8::String::Utf8Value utf8(isolate, value);
    return *utf8 ? std::string(*utf8, static_cast<size_t>(utf8.length())) : std::string();
}

LogLevel toLogLevel(int consoleLevel) {
    switch (consoleLevel) {
    case 0:
        return LogLevel::Debug;
    case 2:
        return LogLevel::Warning;
    case 3:
        return LogLevel::Error;
    default:
        return LogLevel::Info;
    }
}

} // namespace

std::unique_ptr<IJavaScriptRuntime> V8Runtime::create(View& view) { return std::make_unique<V8Runtime>(view); }

V8Runtime::V8Runtime(View& view) : view_(view) {}

V8Runtime::~V8Runtime() { dispose(); }

bool V8Runtime::initialize() {
    if (isolate_) {
        return true;
    }
    if (!V8Platform::instance().ensureInitialized()) {
        return false;
    }

    allocator_.reset(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams params;
    params.array_buffer_allocator = allocator_.get();
    isolate_ = v8::Isolate::New(params);
    if (!isolate_) {
        XGU_LOG_ERROR("V8: Isolate::New failed");
        allocator_.reset();
        return false;
    }
    isolate_->SetData(kIsolateDataSlot, this);
    isolate_->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
    isolate_->SetPromiseRejectCallback(&V8Runtime::promiseRejectCallback);

    v8::Isolate::Scope isolateScope(isolate_);
    v8::HandleScope handleScope(isolate_);
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate_);
    v8::Local<v8::Context> context = v8::Context::New(isolate_, nullptr, global);
    if (context.IsEmpty()) {
        XGU_LOG_ERROR("V8: Context::New failed");
        dispose();
        return false;
    }
    context_.Reset(isolate_, context);
    {
        // V8 bootstraps its own no-op `console`; replace it after context creation.
        v8::Context::Scope contextScope(context);
        installGlobals(context);
    }
    XGU_LOG_DEBUG("view \"%s\": V8 isolate created", view_.desc().name.c_str());
    return true;
}

void V8Runtime::installGlobals(v8::Local<v8::Context> context) {
    // console.{debug,log,info,warn,error} -> log sink -> Unity Console.
    v8::Local<v8::ObjectTemplate> consoleTemplate = v8::ObjectTemplate::New(isolate_);
    const struct {
        const char* name;
        int level;
    } methods[] = {{"debug", 0}, {"log", 1}, {"info", 1}, {"warn", 2}, {"error", 3}};
    for (const auto& method : methods) {
        consoleTemplate->Set(isolate_, method.name,
                             v8::FunctionTemplate::New(isolate_, &V8Runtime::consoleCallback,
                                                       v8::Int32::New(isolate_, method.level)));
    }
    v8::Local<v8::Object> console;
    if (!consoleTemplate->NewInstance(context).ToLocal(&console)) {
        XGU_LOG_ERROR("V8: failed to create the console object");
        return;
    }
    v8::Local<v8::Object> global = context->Global();
    global->Set(context, v8::String::NewFromUtf8Literal(isolate_, "console"), console).Check();
    // `window` and `globalThis` name the same global object (DOM globals arrive in Stage 3).
    global->Set(context, v8::String::NewFromUtf8Literal(isolate_, "window"), global).Check();
}

void V8Runtime::evaluate(std::string_view source, std::string_view originName) {
    if (!isolate_) {
        return;
    }
    v8::Isolate::Scope isolateScope(isolate_);
    v8::HandleScope handleScope(isolate_);
    v8::Local<v8::Context> context = context_.Get(isolate_);
    v8::Context::Scope contextScope(context);
    v8::TryCatch tryCatch(isolate_);

    v8::Local<v8::String> sourceString;
    if (!v8::String::NewFromUtf8(isolate_, source.data(), v8::NewStringType::kNormal, static_cast<int>(source.size()))
             .ToLocal(&sourceString)) {
        XGU_LOG_ERROR("V8: script source is not valid UTF-8 or too large (%zu bytes)", source.size());
        return;
    }
    v8::Local<v8::String> nameString =
        v8::String::NewFromUtf8(isolate_, originName.empty() ? "<anonymous>" : originName.data(),
                                v8::NewStringType::kNormal, originName.empty() ? -1 : static_cast<int>(originName.size()))
            .ToLocalChecked();
    v8::ScriptOrigin origin(nameString);

    v8::Local<v8::Script> script;
    if (!v8::Script::Compile(context, sourceString, &origin).ToLocal(&script)) {
        reportException(tryCatch, context);
        return;
    }
    v8::Local<v8::Value> result;
    if (!script->Run(context).ToLocal(&result)) {
        reportException(tryCatch, context);
    }
    isolate_->PerformMicrotaskCheckpoint();
    if (tryCatch.HasCaught()) {
        reportException(tryCatch, context);
    }
}

void V8Runtime::tick(double) {
    if (!isolate_) {
        return;
    }
    v8::Isolate::Scope isolateScope(isolate_);
    v8::HandleScope handleScope(isolate_);
    v8::Local<v8::Context> context = context_.Get(isolate_);
    v8::Context::Scope contextScope(context);
    v8::Platform* platform = V8Platform::instance().platform();
    for (int i = 0; i < 64 && platform && v8::platform::PumpMessageLoop(platform, isolate_); ++i) {
    }
    isolate_->PerformMicrotaskCheckpoint();
}

void V8Runtime::dispose() {
    if (!isolate_) {
        return;
    }
    {
        v8::Isolate::Scope isolateScope(isolate_);
        context_.Reset();
    }
    isolate_->Dispose();
    isolate_ = nullptr;
    allocator_.reset();
    XGU_LOG_DEBUG("view \"%s\": V8 isolate disposed", view_.desc().name.c_str());
}

std::string V8Runtime::formatValue(v8::Local<v8::Context> context, v8::Local<v8::Value> value) {
    if (value.IsEmpty()) {
        return "undefined";
    }
    if (value->IsString()) {
        return toUtf8(isolate_, value);
    }
    if (value->IsNativeError()) {
        v8::TryCatch guard(isolate_);
        v8::Local<v8::Value> stack;
        if (value.As<v8::Object>()
                ->Get(context, v8::String::NewFromUtf8Literal(isolate_, "stack"))
                .ToLocal(&stack) &&
            stack->IsString()) {
            return toUtf8(isolate_, stack);
        }
    }
    if (value->IsObject() && !value->IsFunction()) {
        v8::TryCatch guard(isolate_);
        v8::Local<v8::String> json;
        if (v8::JSON::Stringify(context, value).ToLocal(&json) && json->Length() > 0) {
            return toUtf8(isolate_, json);
        }
    }
    v8::TryCatch guard(isolate_);
    v8::Local<v8::String> text;
    if (value->ToDetailString(context).ToLocal(&text)) {
        return toUtf8(isolate_, text);
    }
    return "<unprintable>";
}

void V8Runtime::reportException(v8::TryCatch& tryCatch, v8::Local<v8::Context> context) {
    if (!tryCatch.HasCaught()) {
        return;
    }
    std::string text;
    v8::Local<v8::Value> stack;
    if (tryCatch.StackTrace(context).ToLocal(&stack) && stack->IsString()) {
        text = toUtf8(isolate_, stack);
    } else {
        text = formatValue(context, tryCatch.Exception());
    }
    v8::Local<v8::Message> message = tryCatch.Message();
    if (!message.IsEmpty() && text.find("\n    at ") == std::string::npos) {
        const std::string resource = toUtf8(isolate_, message->GetScriptResourceName());
        const int line = message->GetLineNumber(context).FromMaybe(0);
        const int column = message->GetStartColumn(context).FromMaybe(-1) + 1;
        text += " (" + resource + ":" + std::to_string(line) + ":" + std::to_string(column) + ")";
    }
    Log::write(LogLevel::Error, "Uncaught " + text);
    tryCatch.Reset();
}

void V8Runtime::consoleCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::HandleScope handleScope(isolate);
    auto* self = static_cast<V8Runtime*>(isolate->GetData(kIsolateDataSlot));
    if (!self) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const int level = info.Data().As<v8::Int32>()->Value();
    std::string line;
    for (int i = 0; i < info.Length(); ++i) {
        if (i > 0) {
            line.push_back(' ');
        }
        line += self->formatValue(context, info[i]);
    }
    Log::write(toLogLevel(level), line);
}

void V8Runtime::promiseRejectCallback(v8::PromiseRejectMessage message) {
    if (message.GetEvent() != v8::kPromiseRejectWithNoHandler) {
        return;
    }
    v8::Isolate* isolate = message.GetPromise()->GetIsolate();
    v8::HandleScope handleScope(isolate);
    auto* self = static_cast<V8Runtime*>(isolate->GetData(kIsolateDataSlot));
    if (!self) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    Log::write(LogLevel::Error, "Unhandled promise rejection: " + self->formatValue(context, message.GetValue()));
}

} // namespace xgu::js
