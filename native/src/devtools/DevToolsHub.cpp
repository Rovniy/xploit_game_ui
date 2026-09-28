#include "devtools/DevToolsHub.h"

#include "core/Log.h"
#include "devtools/DevToolsServer.h"

#include <cstdio>

#ifndef XGU_VERSION_STRING
#define XGU_VERSION_STRING "dev"
#endif

namespace xgu::devtools {
namespace {

void appendJsonString(std::string& out, const std::string& text) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
                out += escaped;
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

// "/devtools/page/<id>" (what the discovery list advertises) or "/<id>".
bool targetFromPath(const std::string& path, TargetId& out) {
    std::string id = path;
    const std::string prefix = "/devtools/page/";
    if (id.rfind(prefix, 0) == 0) {
        id = id.substr(prefix.size());
    } else if (!id.empty() && id.front() == '/') {
        id = id.substr(1);
    }
    if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    out = std::stoull(id);
    return true;
}

} // namespace

DevToolsHub& DevToolsHub::instance() {
    static DevToolsHub* hub = new DevToolsHub(); // intentionally leaked, like Runtime
    return *hub;
}

DevToolsHub::DevToolsHub() : server_(std::make_unique<DevToolsServer>(*this)) {}

DevToolsHub::~DevToolsHub() = default;

void DevToolsHub::configure(std::function<void(std::function<void()>)> post) {
    std::lock_guard lock(mutex_);
    post_ = std::move(post);
    stopping_ = false;
}

bool DevToolsHub::drainsOnTick() const {
    std::lock_guard lock(mutex_);
    return !post_;
}

// --- server ------------------------------------------------------------------------

bool DevToolsHub::start(uint16_t port) {
    if (server_->running()) {
        return server_->port() == port || port == 0;
    }
    if (!server_->start(port)) {
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        port_ = server_->port();
    }
    XGU_LOG_INFO("DevTools listening on 127.0.0.1:%u (chrome://inspect, or devtools://.../js_app.html?ws=...)",
                 static_cast<unsigned>(server_->port()));
    return true;
}

void DevToolsHub::stop() {
    if (!server_->running()) {
        return;
    }
    server_->stop(); // reports every remote connection as closed
    std::lock_guard lock(mutex_);
    port_ = 0;
    XGU_LOG_INFO("DevTools stopped");
}

uint16_t DevToolsHub::port() const {
    std::lock_guard lock(mutex_);
    return port_;
}

std::string DevToolsHub::targetUrl(TargetId id, const char* page) const {
    return std::string("devtools://devtools/bundled/") + page + "?experiments=true&v8only=true&ws=127.0.0.1:" +
           std::to_string(port_) + "/devtools/page/" + std::to_string(id);
}

std::string DevToolsHub::frontendUrl(TargetId target) const {
    std::lock_guard lock(mutex_);
    if (port_ == 0 || targets_.find(target) == targets_.end()) {
        return {};
    }
    return targetUrl(target, "js_app.html");
}

std::string DevToolsHub::httpResponse(const std::string& path, int& status) const {
    std::lock_guard lock(mutex_);
    status = 200;
    if (path == "/json" || path == "/json/list") {
        std::string out = "[";
        bool first = true;
        for (const auto& [id, target] : targets_) {
            if (!target.inspector) {
                continue; // between two documents
            }
            if (!first) {
                out += ",";
            }
            first = false;
            const std::string idText = std::to_string(id);
            out += "{\"description\":\"xploit_game_ui view\",\"devtoolsFrontendUrl\":";
            appendJsonString(out, targetUrl(id, "js_app.html"));
            out += ",\"devtoolsFrontendUrlCompat\":";
            appendJsonString(out, targetUrl(id, "inspector.html"));
            out += ",\"faviconUrl\":\"\",\"id\":";
            appendJsonString(out, idText);
            out += ",\"title\":";
            appendJsonString(out, target.title);
            out += ",\"type\":\"node\",\"url\":";
            appendJsonString(out, target.url);
            out += ",\"webSocketDebuggerUrl\":";
            appendJsonString(out, "ws://127.0.0.1:" + std::to_string(port_) + "/devtools/page/" + idText);
            out += "}";
        }
        out += "]";
        return out;
    }
    if (path == "/json/version") {
        return std::string("{\"Browser\":\"xploit_game_ui/") + XGU_VERSION_STRING + "\",\"Protocol-Version\":\"1.3\"}";
    }
    status = 404;
    return "{\"error\":\"not found\"}";
}

bool DevToolsHub::hasTarget(const std::string& path) const {
    TargetId id = 0;
    if (!targetFromPath(path, id)) {
        return false;
    }
    std::lock_guard lock(mutex_);
    const auto it = targets_.find(id);
    return it != targets_.end() && it->second.inspector != nullptr;
}

bool DevToolsHub::openConnection(ConnectionId connection, const std::string& path) {
    TargetId id = 0;
    if (!targetFromPath(path, id)) {
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        if (targets_.find(id) == targets_.end()) {
            return false;
        }
        connections_[connection] = Connection{id, false};
        push(Event{EventKind::Attach, id, connection, {}, {}});
    }
    requestDrain(id);
    return true;
}

void DevToolsHub::receive(ConnectionId connection, std::string message) {
    TargetId id = 0;
    {
        std::lock_guard lock(mutex_);
        const auto it = connections_.find(connection);
        if (it == connections_.end()) {
            return;
        }
        id = it->second.target;
        push(Event{EventKind::Message, id, connection, std::move(message), {}});
    }
    requestDrain(id);
}

void DevToolsHub::closeConnection(ConnectionId connection) {
    TargetId id = 0;
    {
        std::lock_guard lock(mutex_);
        const auto it = connections_.find(connection);
        if (it == connections_.end()) {
            return;
        }
        id = it->second.target;
        connections_.erase(it);
        suspendedStates_.erase(connection);
        push(Event{EventKind::Detach, id, connection, {}, {}});
    }
    requestDrain(id);
}

// --- host session ---------------------------------------------------------------

void DevToolsHub::sendFromHost(TargetId target, std::string message) {
    bool inlineRuntime = false;
    {
        std::lock_guard lock(mutex_);
        auto it = hostConnections_.find(target);
        if (it == hostConnections_.end()) {
            const ConnectionId connection = nextHostConnection_++;
            it = hostConnections_.emplace(target, connection).first;
            connections_[connection] = Connection{target, true};
            push(Event{EventKind::Attach, target, connection, {}, {}});
        }
        push(Event{EventKind::Message, target, it->second, std::move(message), {}});
        inlineRuntime = !post_;
    }
    if (inlineRuntime) {
        // The single-threaded runtime is this very thread: answer right away.
        drain(target);
    } else {
        requestDrain(target);
    }
}

bool DevToolsHub::pollHost(TargetId& target, std::string& message) {
    std::lock_guard lock(mutex_);
    if (hostOutbox_.empty()) {
        return false;
    }
    target = hostOutbox_.front().first;
    message = std::move(hostOutbox_.front().second);
    hostOutbox_.pop_front();
    return true;
}

SendFn DevToolsHub::senderFor(ConnectionId connection, bool host) {
    if (host) {
        return [this, connection](std::string message) {
            std::lock_guard lock(mutex_);
            const auto it = connections_.find(connection);
            if (it != connections_.end()) {
                hostOutbox_.emplace_back(it->second.target, std::move(message));
            }
        };
    }
    DevToolsServer* server = server_.get();
    return [server, connection](std::string message) { server->send(connection, message); };
}

// --- runtime thread ---------------------------------------------------------------

void DevToolsHub::push(Event event, bool front) {
    if (front) {
        events_.push_front(std::move(event));
    } else {
        events_.push_back(std::move(event));
    }
    cv_.notify_all();
}

void DevToolsHub::requestDrain(TargetId id) {
    std::function<void(std::function<void()>)> post;
    {
        std::lock_guard lock(mutex_);
        post = post_;
    }
    if (post) {
        post([this, id] { drain(id); });
    }
    // Without a runtime thread, tick() drains (never from the server thread).
}

void DevToolsHub::addTarget(TargetId id, IInspectorTarget* target, std::string title, std::string url) {
    {
        std::lock_guard lock(mutex_);
        targets_[id] = Target{target, std::move(title), std::move(url)};
        // Connections that outlived the previous document come back first, with
        // what their sessions had enabled, before anything they sent meanwhile.
        for (auto it = connections_.rbegin(); it != connections_.rend(); ++it) {
            if (it->second.target != id) {
                continue;
            }
            SessionState state;
            if (const auto saved = suspendedStates_.find(it->first); saved != suspendedStates_.end()) {
                state = std::move(saved->second);
                suspendedStates_.erase(saved);
            }
            push(Event{EventKind::Attach, id, it->first, {}, std::move(state)}, true);
        }
    }
    requestDrain(id);
}

void DevToolsHub::suspendTarget(TargetId id, std::vector<std::pair<ConnectionId, SessionState>> states) {
    std::lock_guard lock(mutex_);
    const auto it = targets_.find(id);
    if (it != targets_.end()) {
        it->second.inspector = nullptr;
    }
    for (auto& [connection, state] : states) {
        if (connections_.find(connection) != connections_.end()) {
            suspendedStates_[connection] = std::move(state);
        }
    }
    // Whatever was still queued was addressed to the old document's sessions;
    // attaches are re-issued by addTarget.
    std::erase_if(events_, [id](const Event& event) {
        return event.target == id && event.kind != EventKind::Message;
    });
}

void DevToolsHub::forgetTarget(TargetId id) {
    std::vector<ConnectionId> remote;
    {
        std::lock_guard lock(mutex_);
        targets_.erase(id);
        for (auto it = connections_.begin(); it != connections_.end();) {
            if (it->second.target != id) {
                ++it;
                continue;
            }
            if (!it->second.host) {
                remote.push_back(it->first);
            }
            suspendedStates_.erase(it->first);
            it = connections_.erase(it);
        }
        hostConnections_.erase(id);
        std::erase_if(events_, [id](const Event& event) { return event.target == id; });
    }
    for (const ConnectionId connection : remote) {
        server_->close(connection);
    }
}

std::vector<DevToolsHub::Event> DevToolsHub::takeEvents(TargetId id) {
    std::vector<Event> taken;
    const auto target = targets_.find(id);
    if (target == targets_.end() || !target->second.inspector) {
        return taken; // kept until the view has a runtime again
    }
    for (auto it = events_.begin(); it != events_.end();) {
        if (it->target == id) {
            taken.push_back(std::move(*it));
            it = events_.erase(it);
        } else {
            ++it;
        }
    }
    return taken;
}

void DevToolsHub::handle(TargetId id, std::vector<Event>& events) {
    for (Event& event : events) {
        IInspectorTarget* inspector = nullptr;
        bool host = false;
        {
            std::lock_guard lock(mutex_);
            const auto target = targets_.find(id);
            if (target == targets_.end() || !target->second.inspector) {
                return; // a message ended the runtime (a reload from the console)
            }
            inspector = target->second.inspector;
            if (const auto connection = connections_.find(event.connection); connection != connections_.end()) {
                host = connection->second.host;
            }
        }
        switch (event.kind) {
        case EventKind::Attach:
            inspector->attach(event.connection, senderFor(event.connection, host), event.state);
            break;
        case EventKind::Message:
            inspector->dispatch(event.connection, event.text);
            break;
        case EventKind::Detach:
            inspector->detach(event.connection);
            break;
        }
    }
}

void DevToolsHub::drain(TargetId id) {
    for (;;) {
        std::vector<Event> events;
        {
            std::lock_guard lock(mutex_);
            events = takeEvents(id);
        }
        if (events.empty()) {
            return;
        }
        handle(id, events);
    }
}

void DevToolsHub::attachPending(TargetId id) {
    std::vector<Event> attaches;
    {
        std::lock_guard lock(mutex_);
        const auto target = targets_.find(id);
        if (target == targets_.end() || !target->second.inspector) {
            return;
        }
        for (auto it = events_.begin(); it != events_.end();) {
            if (it->target == id && it->kind == EventKind::Attach) {
                attaches.push_back(std::move(*it));
                it = events_.erase(it);
            } else {
                ++it;
            }
        }
    }
    handle(id, attaches);
}

void DevToolsHub::drainAll() {
    std::vector<TargetId> ids;
    {
        std::lock_guard lock(mutex_);
        if (events_.empty()) {
            return;
        }
        for (const Event& event : events_) {
            ids.push_back(event.target);
        }
    }
    for (const TargetId id : ids) {
        drain(id);
    }
}

bool DevToolsHub::waitAndDrain(TargetId id, const std::function<bool()>& done) {
    std::vector<Event> events;
    {
        std::unique_lock lock(mutex_);
        const auto hasWork = [&] {
            for (const Event& event : events_) {
                if (event.target == id) {
                    return true;
                }
            }
            return false;
        };
        cv_.wait(lock, [&] { return stopping_ || hasWork() || done(); });
        if (stopping_) {
            return false;
        }
        events = takeEvents(id);
    }
    handle(id, events);
    return true;
}

void DevToolsHub::shutdown() {
    stop();
    std::lock_guard lock(mutex_);
    stopping_ = true;
    cv_.notify_all();
    events_.clear();
    connections_.clear();
    hostConnections_.clear();
    suspendedStates_.clear();
    hostOutbox_.clear();
}

} // namespace xgu::devtools
