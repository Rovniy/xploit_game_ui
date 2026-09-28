#include "js/v8/Inspector.h"

#include "core/Log.h"
#include "js/v8/V8Runtime.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace xgu::js {
namespace {

using v8_inspector::StringView;

// Each view has its own isolate and inspector, so one group is all there is.
constexpr int kContextGroupId = 1;

std::u16string utf8ToUtf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        uint32_t code = 0xFFFD;
        size_t length = 1;
        if (byte < 0x80) {
            code = byte;
        } else if ((byte >> 5) == 0x6 && i + 1 < text.size()) {
            code = ((byte & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
            length = 2;
        } else if ((byte >> 4) == 0xE && i + 2 < text.size()) {
            code = ((byte & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                   (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
            length = 3;
        } else if ((byte >> 3) == 0x1E && i + 3 < text.size()) {
            code = ((byte & 0x07u) << 18) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 12) |
                   ((static_cast<unsigned char>(text[i + 2]) & 0x3Fu) << 6) |
                   (static_cast<unsigned char>(text[i + 3]) & 0x3Fu);
            length = 4;
        }
        if (code >= 0x10000) {
            code -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (code >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(code));
        }
        i += length;
    }
    return out;
}

std::string toUtf8(const StringView& view) {
    std::string out;
    if (view.is8Bit()) {
        // Latin-1; what V8 hands out here is plain ASCII JSON in practice.
        for (size_t i = 0; i < view.length(); ++i) {
            const uint8_t c = view.characters8()[i];
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        return out;
    }
    const uint16_t* chars = view.characters16();
    for (size_t i = 0; i < view.length(); ++i) {
        uint32_t code = chars[i];
        if (code >= 0xD800 && code < 0xDC00 && i + 1 < view.length() && chars[i + 1] >= 0xDC00 &&
            chars[i + 1] < 0xE000) {
            code = 0x10000 + ((code - 0xD800) << 10) + (chars[i + 1] - 0xDC00);
            ++i;
        }
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    return out;
}

StringView viewOf(const std::u16string& text) {
    return StringView(reinterpret_cast<const uint16_t*>(text.data()), text.size());
}

LogLevel toLogLevel(v8::Isolate::MessageErrorLevel level) {
    switch (level) {
    case v8::Isolate::kMessageDebug:
        return LogLevel::Debug;
    case v8::Isolate::kMessageWarning:
        return LogLevel::Warning;
    case v8::Isolate::kMessageError:
        return LogLevel::Error;
    default:
        return LogLevel::Info;
    }
}

} // namespace

// Carries what a session says back to its client.
class Inspector::Channel final : public v8_inspector::V8Inspector::Channel {
public:
    explicit Channel(devtools::SendFn send) : send_(std::move(send)) {}

    void sendResponse(int, std::unique_ptr<v8_inspector::StringBuffer> message) override {
        send_(toUtf8(message->string()));
    }
    void sendNotification(std::unique_ptr<v8_inspector::StringBuffer> message) override {
        send_(toUtf8(message->string()));
    }
    void flushProtocolNotifications() override {}

private:
    devtools::SendFn send_;
};

Inspector::Inspector(V8Runtime& runtime, uint64_t viewId, std::string title, std::string url)
    : runtime_(runtime), viewId_(viewId), title_(std::move(title)), url_(std::move(url)) {
    inspector_ = v8_inspector::V8Inspector::create(runtime_.isolate(), this);
}

Inspector::~Inspector() { shutdown(); }

void Inspector::contextCreated(v8::Local<v8::Context> context) {
    const std::u16string name = utf8ToUtf16(title_);
    v8_inspector::V8ContextInfo info(context, kContextGroupId, viewOf(name));
    inspector_->contextCreated(info);
    auto& hub = devtools::DevToolsHub::instance();
    hub.addTarget(viewId_, this, title_, url_);
    registered_ = true;
    // Right away, not through the queue: the document's scripts run before the
    // runtime thread would get to it, and a debugger that survived a reload
    // has to be attached, breakpoints and all, when they do.
    hub.attachPending(viewId_);
}

void Inspector::contextDestroyed(v8::Local<v8::Context> context) { inspector_->contextDestroyed(context); }

void Inspector::shutdown() {
    if (!registered_) {
        sessions_.clear();
        return;
    }
    registered_ = false;
    std::vector<std::pair<devtools::ConnectionId, devtools::SessionState>> states;
    for (auto& [connection, session] : sessions_) {
        states.emplace_back(connection, session.session->state());
    }
    {
        v8::Isolate* isolate = runtime_.isolate();
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        sessions_.clear();
    }
    devtools::DevToolsHub::instance().suspendTarget(viewId_, std::move(states));
}

void Inspector::exceptionThrown(v8::Local<v8::Context> context, const std::string& headline,
                                v8::Local<v8::Value> exception, v8::Local<v8::Message> message,
                                const std::string& detail) {
    std::unique_ptr<v8_inspector::V8StackTrace> trace;
    int scriptId = 0;
    unsigned line = 0;
    unsigned column = 0;
    std::string url;
    if (!message.IsEmpty()) {
        v8::Local<v8::StackTrace> stack = message->GetStackTrace();
        if (!stack.IsEmpty() && stack->GetFrameCount() > 0) {
            trace = inspector_->createStackTrace(stack);
        }
        scriptId = message->GetScriptOrigin().ScriptId();
        line = static_cast<unsigned>(message->GetLineNumber(context).FromMaybe(0));
        column = static_cast<unsigned>(message->GetStartColumn(context).FromMaybe(0) + 1);
        v8::String::Utf8Value resource(runtime_.isolate(), message->GetScriptResourceName());
        if (*resource) {
            url.assign(*resource, static_cast<size_t>(resource.length()));
        }
    }
    const std::u16string headline16 = utf8ToUtf16(headline);
    const std::u16string detail16 = utf8ToUtf16(detail);
    const std::u16string url16 = utf8ToUtf16(url);
    inspector_->exceptionThrown(context, viewOf(headline16), exception, viewOf(detail16), viewOf(url16), line, column,
                                std::move(trace), scriptId);
}

// --- sessions -------------------------------------------------------------------------

void Inspector::attach(devtools::ConnectionId connection, devtools::SendFn send, const devtools::SessionState& state) {
    v8::Isolate* isolate = runtime_.isolate();
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handleScope(isolate);
    v8::Context::Scope contextScope(runtime_.context());
    const LogViewScope logScope(viewId_);

    sessions_.erase(connection);
    Session session;
    session.channel = std::make_unique<Channel>(std::move(send));
    // The saved state is CBOR; V8 takes it as an 8-bit view.
    const StringView stateView(state.data(), state.size());
    session.session = inspector_->connect(kContextGroupId, session.channel.get(), stateView,
                                          v8_inspector::V8Inspector::kFullyTrusted,
                                          v8_inspector::V8Inspector::kNotWaitingForDebugger);
    if (!session.session) {
        XGU_LOG_ERROR("DevTools: the V8 inspector refused a session");
        return;
    }
    sessions_.emplace(connection, std::move(session));
}

void Inspector::detach(devtools::ConnectionId connection) {
    v8::Isolate* isolate = runtime_.isolate();
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handleScope(isolate);
    // Closing the last debugging session also resumes a paused script.
    sessions_.erase(connection);
}

void Inspector::dispatch(devtools::ConnectionId connection, const std::string& message) {
    const auto it = sessions_.find(connection);
    if (it == sessions_.end()) {
        return;
    }
    v8::Isolate* isolate = runtime_.isolate();
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handleScope(isolate);
    v8::Context::Scope contextScope(runtime_.context());
    const LogViewScope logScope(viewId_);

    const std::u16string text = utf8ToUtf16(message);
    it->second.session->dispatchProtocolMessage(viewOf(text));
    // An evaluation from the console may have queued promise reactions. Not
    // while paused, though: that would run script in the middle of the frame
    // the debugger is stopped in.
    if (!paused_) {
        runtime_.performMicrotaskCheckpoint();
    }
}

// --- V8InspectorClient ---------------------------------------------------------------

void Inspector::runMessageLoopOnPause(int) {
    if (paused_) {
        return; // already inside the loop
    }
    paused_ = true;
    quitPause_ = false;
    auto& hub = devtools::DevToolsHub::instance();
    // Only this view's messages are handled here; the runtime thread's own
    // tasks (frames, timers, other views) wait until the script resumes.
    while (!quitPause_) {
        if (!hub.waitAndDrain(viewId_, [this] { return quitPause_; })) {
            break; // shutting down
        }
    }
    paused_ = false;
    quitPause_ = false;
}

void Inspector::quitMessageLoopOnPause() { quitPause_ = true; }

v8::Local<v8::Context> Inspector::ensureDefaultContextInGroup(int) { return runtime_.context(); }

void Inspector::consoleAPIMessage(int, v8::Isolate::MessageErrorLevel level, const StringView& message,
                                  const StringView&, unsigned, unsigned, v8_inspector::V8StackTrace*) {
    if (!consoleEcho_) {
        return;
    }
    // console.table, console.assert, console.count and the rest, which the
    // wrappers do not cover, reach the log through here.
    Log::write(toLogLevel(level), toUtf8(message));
}

double Inspector::currentTimeMS() {
    using namespace std::chrono;
    return static_cast<double>(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count()) / 1000.0;
}

} // namespace xgu::js
