#include "js/v8/UnityBindings.h"

#include "core/Log.h"
#include "js/v8/V8Runtime.h"

namespace xgu::js {
namespace {

std::string toUtf8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
    if (value.IsEmpty()) {
        return {};
    }
    v8::String::Utf8Value utf8(isolate, value);
    return *utf8 ? std::string(*utf8, static_cast<size_t>(utf8.length())) : std::string();
}

v8::Local<v8::String> toV8(v8::Isolate* isolate, std::string_view text) {
    return v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal, static_cast<int>(text.size()))
        .FromMaybe(v8::String::Empty(isolate));
}

// The bindings behind the `Unity` object the callback was reached through.
UnityBindings* bindingsOf(v8::Isolate* isolate) {
    V8Runtime* runtime = V8Runtime::fromIsolate(isolate);
    return runtime ? runtime->unityBindings() : nullptr;
}

void throwTypeError(v8::Isolate* isolate, const char* message) {
    isolate->ThrowException(v8::Exception::TypeError(toV8(isolate, message)));
}

} // namespace

UnityBindings::UnityBindings(V8Runtime& runtime, v8::Isolate* isolate, IBridge& bridge)
    : runtime_(runtime), isolate_(isolate), bridge_(bridge) {}

UnityBindings::~UnityBindings() { dispose(); }

void UnityBindings::install(v8::Local<v8::Context> context) {
    v8::HandleScope scope(isolate_);
    v8::Local<v8::Object> unity = v8::Object::New(isolate_);

    const auto method = [&](const char* name, v8::FunctionCallback callback) {
        v8::Local<v8::Function> function;
        if (v8::FunctionTemplate::New(isolate_, callback)->GetFunction(context).ToLocal(&function)) {
            unity->Set(context, toV8(isolate_, name), function).Check();
        }
    };
    method("emit", emitCallback);
    method("on", onCallback);
    method("off", offCallback);
    method("call", callCallback);

    context->Global()->Set(context, toV8(isolate_, "Unity"), unity).Check();
}

void UnityBindings::dispose() {
    // Reject what is still waiting, so page code with `await Unity.call(...)`
    // sees a failure instead of hanging for ever.
    if (!pendingCalls_.empty() && isolate_) {
        v8::HandleScope scope(isolate_);
        v8::Local<v8::Context> context = runtime_.context();
        if (!context.IsEmpty()) {
            v8::Context::Scope contextScope(context);
            for (auto& [id, resolver] : pendingCalls_) {
                if (resolver.IsEmpty()) {
                    continue;
                }
                const v8::Local<v8::Value> error =
                    v8::Exception::Error(toV8(isolate_, "the view was closed before the call returned"));
                (void)resolver.Get(isolate_)->Reject(context, error);
            }
        }
    }
    pendingCalls_.clear();
    listeners_.clear();
}

bool UnityBindings::argumentsToJson(v8::Local<v8::Context> context,
                                    const v8::FunctionCallbackInfo<v8::Value>& info, int first, std::string& out) {
    const int count = info.Length() - first;
    if (count <= 0) {
        out.clear();
        return true;
    }
    v8::Local<v8::Array> array = v8::Array::New(isolate_, count);
    for (int i = 0; i < count; ++i) {
        if (array->Set(context, static_cast<uint32_t>(i), info[first + i]).IsNothing()) {
            return false;
        }
    }
    v8::Local<v8::String> json;
    if (!v8::JSON::Stringify(context, array).ToLocal(&json)) {
        return false; // a function, a cyclic object, ... the exception is pending
    }
    out = toUtf8(isolate_, json);
    return true;
}

bool UnityBindings::jsonToArguments(v8::Local<v8::Context> context, const std::string& json,
                                    std::vector<v8::Local<v8::Value>>& out) {
    if (json.empty()) {
        return true;
    }
    v8::Local<v8::Value> parsed;
    if (!v8::JSON::Parse(context, toV8(isolate_, json)).ToLocal(&parsed)) {
        XGU_LOG_ERROR("bridge: the host sent a payload that is not valid JSON");
        return false;
    }
    if (!parsed->IsArray()) {
        out.push_back(parsed);
        return true;
    }
    v8::Local<v8::Array> array = parsed.As<v8::Array>();
    for (uint32_t i = 0; i < array->Length(); ++i) {
        v8::Local<v8::Value> value;
        if (array->Get(context, i).ToLocal(&value)) {
            out.push_back(value);
        }
    }
    return true;
}

void UnityBindings::emitCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    UnityBindings* bindings = bindingsOf(isolate);
    if (!bindings || info.Length() < 1) {
        throwTypeError(isolate, "Unity.emit needs an event name");
        return;
    }
    BridgeMessage message;
    message.kind = BridgeMessageKind::Emit;
    message.name = toUtf8(isolate, info[0]);
    if (!bindings->argumentsToJson(isolate->GetCurrentContext(), info, 1, message.json)) {
        return; // the conversion left an exception pending
    }
    bindings->bridge_.postToHost(std::move(message));
}

void UnityBindings::onCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    UnityBindings* bindings = bindingsOf(isolate);
    if (!bindings || info.Length() < 2 || !info[1]->IsFunction()) {
        throwTypeError(isolate, "Unity.on needs an event name and a function");
        return;
    }
    auto& handlers = bindings->listeners_[toUtf8(isolate, info[0])];
    handlers.emplace_back(isolate, info[1].As<v8::Function>());
}

void UnityBindings::offCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    UnityBindings* bindings = bindingsOf(isolate);
    if (!bindings || info.Length() < 1) {
        throwTypeError(isolate, "Unity.off needs an event name");
        return;
    }
    const auto it = bindings->listeners_.find(toUtf8(isolate, info[0]));
    if (it == bindings->listeners_.end()) {
        return;
    }
    if (info.Length() < 2 || !info[1]->IsFunction()) {
        bindings->listeners_.erase(it); // no handler given: remove them all
        return;
    }
    v8::Local<v8::Function> target = info[1].As<v8::Function>();
    auto& handlers = it->second;
    for (auto handler = handlers.begin(); handler != handlers.end(); ++handler) {
        if (handler->Get(isolate) == target) {
            handlers.erase(handler);
            break;
        }
    }
    if (handlers.empty()) {
        bindings->listeners_.erase(it);
    }
}

void UnityBindings::callCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    UnityBindings* bindings = bindingsOf(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!bindings || info.Length() < 1) {
        throwTypeError(isolate, "Unity.call needs a function name");
        return;
    }
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return;
    }
    info.GetReturnValue().Set(resolver->GetPromise());

    BridgeMessage message;
    message.kind = BridgeMessageKind::Call;
    message.id = bindings->nextCallId_++;
    message.name = toUtf8(isolate, info[0]);
    if (!bindings->argumentsToJson(context, info, 1, message.json)) {
        // Turn the pending conversion error into a rejection: the caller is
        // awaiting a promise, not a throw.
        v8::TryCatch tryCatch(isolate);
        (void)resolver->Reject(context, v8::Exception::TypeError(toV8(isolate, "arguments are not JSON-serialisable")));
        tryCatch.Reset();
        return;
    }
    const uint64_t id = message.id;
    if (!bindings->bridge_.postToHost(std::move(message))) {
        (void)resolver->Reject(context, v8::Exception::Error(toV8(isolate, "the bridge refused the call")));
        return;
    }
    bindings->pendingCalls_.emplace(id, v8::Global<v8::Promise::Resolver>(isolate, resolver));
}

void UnityBindings::deliverSend(v8::Local<v8::Context> context, const BridgeMessage& message) {
    const auto it = listeners_.find(message.name);
    if (it == listeners_.end() || it->second.empty()) {
        return;
    }
    std::vector<v8::Local<v8::Value>> args;
    if (!jsonToArguments(context, message.json, args)) {
        return;
    }
    // Snapshot: a handler may call Unity.off while it runs.
    std::vector<v8::Local<v8::Function>> handlers;
    handlers.reserve(it->second.size());
    for (const v8::Global<v8::Function>& handler : it->second) {
        handlers.push_back(handler.Get(isolate_));
    }
    for (v8::Local<v8::Function>& handler : handlers) {
        runtime_.callFunction(handler, context->Global(), static_cast<int>(args.size()),
                              args.empty() ? nullptr : args.data());
    }
}

void UnityBindings::deliverReply(v8::Local<v8::Context> context, const BridgeMessage& message) {
    const auto it = pendingCalls_.find(message.id);
    if (it == pendingCalls_.end()) {
        return; // already settled, or from a previous document
    }
    v8::Local<v8::Promise::Resolver> resolver = it->second.Get(isolate_);
    pendingCalls_.erase(it);

    v8::Local<v8::Value> value = v8::Undefined(isolate_);
    if (!message.json.empty()) {
        v8::Local<v8::Value> parsed;
        if (v8::JSON::Parse(context, toV8(isolate_, message.json)).ToLocal(&parsed)) {
            value = parsed;
        }
    }
    if (message.ok) {
        (void)resolver->Resolve(context, value);
        return;
    }
    // The host sends {name, message, stack}; rebuild it as an Error so the page
    // sees a normal rejection.
    v8::Local<v8::Value> reason = value;
    if (value->IsObject()) {
        v8::Local<v8::Object> object = value.As<v8::Object>();
        v8::Local<v8::Value> text;
        if (object->Get(context, toV8(isolate_, "message")).ToLocal(&text) && !text->IsUndefined()) {
            v8::Local<v8::Value> error = v8::Exception::Error(toV8(isolate_, toUtf8(isolate_, text)));
            v8::Local<v8::Value> name;
            if (error->IsObject() && object->Get(context, toV8(isolate_, "name")).ToLocal(&name) &&
                !name->IsUndefined()) {
                (void)error.As<v8::Object>()->Set(context, toV8(isolate_, "name"), name);
            }
            reason = error;
        }
    }
    (void)resolver->Reject(context, reason);
}

void UnityBindings::deliver(const BridgeMessage& message) {
    if (!isolate_) {
        return;
    }
    v8::Isolate::Scope isolateScope(isolate_);
    v8::HandleScope scope(isolate_);
    v8::Local<v8::Context> context = runtime_.context();
    if (context.IsEmpty()) {
        return;
    }
    v8::Context::Scope contextScope(context);

    switch (message.kind) {
    case BridgeMessageKind::Send:
        deliverSend(context, message);
        break;
    case BridgeMessageKind::Reply:
        deliverReply(context, message);
        break;
    case BridgeMessageKind::Emit:
    case BridgeMessageKind::Call:
        break; // page to host only
    }
    isolate_->PerformMicrotaskCheckpoint();
}

} // namespace xgu::js
