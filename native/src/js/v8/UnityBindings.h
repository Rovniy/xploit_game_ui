#pragma once

#include "core/interfaces/IBridge.h"

#include <v8.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace xgu::js {

class V8Runtime;

// The `Unity` global: the page's half of the bridge.
//
//   Unity.emit(name, ...args)   fire and forget, the host receives a WebEvent
//   Unity.on(name, handler)     receives view.Send(name, ...args)
//   Unity.off(name, handler?)   removes one handler, or all of them for a name
//   Unity.call(name, ...args)   returns a Promise the host settles
//
// Arguments cross as a JSON array produced by v8::JSON, so the engine never has
// to understand them and the host can use its own reader.
class UnityBindings {
public:
    UnityBindings(V8Runtime& runtime, v8::Isolate* isolate, IBridge& bridge);
    ~UnityBindings();

    // Installs `Unity` on the context's global object.
    void install(v8::Local<v8::Context> context);
    // Releases every handle; the pending calls are rejected first, so no promise
    // is left hanging when a document is replaced.
    void dispose();

    // Runs one message the host queued.
    void deliver(const BridgeMessage& message);

    IBridge& bridge() const { return bridge_; }

private:
    static void emitCallback(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void onCallback(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void offCallback(const v8::FunctionCallbackInfo<v8::Value>& info);
    static void callCallback(const v8::FunctionCallbackInfo<v8::Value>& info);

    // Serialises the call arguments from `first` onwards into a JSON array.
    // Returns false and leaves an exception pending when a value cannot be
    // converted (a function, a cyclic object).
    bool argumentsToJson(v8::Local<v8::Context> context, const v8::FunctionCallbackInfo<v8::Value>& info, int first,
                         std::string& out);
    // Parses a JSON array into `out`; an empty string means no arguments.
    bool jsonToArguments(v8::Local<v8::Context> context, const std::string& json, std::vector<v8::Local<v8::Value>>& out);

    void deliverSend(v8::Local<v8::Context> context, const BridgeMessage& message);
    void deliverReply(v8::Local<v8::Context> context, const BridgeMessage& message);

    V8Runtime& runtime_;
    v8::Isolate* isolate_;
    IBridge& bridge_;

    // name -> handlers, in registration order.
    std::unordered_map<std::string, std::vector<v8::Global<v8::Function>>> listeners_;
    // Calls waiting for a reply, by id.
    std::unordered_map<uint64_t, v8::Global<v8::Promise::Resolver>> pendingCalls_;
    uint64_t nextCallId_ = 1;
};

} // namespace xgu::js
