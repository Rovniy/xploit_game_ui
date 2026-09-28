#pragma once

#include "devtools/DevToolsHub.h"

#include <v8-inspector.h>
#include <v8.h>

#include <map>
#include <memory>
#include <string>

namespace xgu::js {

class V8Runtime;

// The V8 inspector of one view: what Chrome DevTools and the Unity JS console
// talk to through devtools::DevToolsHub. It exists for every runtime, attached
// or not, because V8's built-in console reports to it; that is also how
// console messages logged before DevTools connects still show up there.
// Runtime thread only.
class Inspector final : public v8_inspector::V8InspectorClient, public devtools::IInspectorTarget {
public:
    Inspector(V8Runtime& runtime, uint64_t viewId, std::string title, std::string url);
    ~Inspector() override;

    // The view's context: announce it once it exists, withdraw it before it goes.
    void contextCreated(v8::Local<v8::Context> context);
    void contextDestroyed(v8::Local<v8::Context> context);
    // Saves every session's state with the hub and closes the sessions. Call
    // before the isolate goes.
    void shutdown();

    // An uncaught error, so DevTools shows it with its stack.
    void exceptionThrown(v8::Local<v8::Context> context, const std::string& headline,
                         v8::Local<v8::Value> exception, v8::Local<v8::Message> message, const std::string& detail);

    // While set, console messages are not echoed to the log: the console
    // wrappers have already written them there in their own format.
    void setConsoleEcho(bool echo) { consoleEcho_ = echo; }

    // --- devtools::IInspectorTarget ----------------------------------------------
    void attach(devtools::ConnectionId connection, devtools::SendFn send,
                const devtools::SessionState& state) override;
    void detach(devtools::ConnectionId connection) override;
    void dispatch(devtools::ConnectionId connection, const std::string& message) override;

    // --- v8_inspector::V8InspectorClient -----------------------------------------
    void runMessageLoopOnPause(int contextGroupId) override;
    void quitMessageLoopOnPause() override;
    v8::Local<v8::Context> ensureDefaultContextInGroup(int contextGroupId) override;
    void consoleAPIMessage(int contextGroupId, v8::Isolate::MessageErrorLevel level,
                           const v8_inspector::StringView& message, const v8_inspector::StringView& url,
                           unsigned lineNumber, unsigned columnNumber, v8_inspector::V8StackTrace*) override;
    double currentTimeMS() override;

private:
    class Channel;
    struct Session {
        std::unique_ptr<Channel> channel;
        std::unique_ptr<v8_inspector::V8InspectorSession> session;
    };

    V8Runtime& runtime_;
    uint64_t viewId_;
    std::string title_;
    std::string url_;
    std::unique_ptr<v8_inspector::V8Inspector> inspector_;
    std::map<devtools::ConnectionId, Session> sessions_;
    bool registered_ = false;
    bool paused_ = false;
    bool quitPause_ = false;
    bool consoleEcho_ = true;
};

} // namespace xgu::js
