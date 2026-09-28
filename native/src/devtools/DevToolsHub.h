#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace xgu::devtools {

class DevToolsServer;

using ConnectionId = uint64_t;
using TargetId = uint64_t; // the view id
using SendFn = std::function<void(std::string)>;
using SessionState = std::vector<uint8_t>;

// What the hub drives on the runtime thread: one per view with a JavaScript
// runtime (js::Inspector). Every call happens on the runtime thread.
class IInspectorTarget {
public:
    virtual ~IInspectorTarget() = default;
    // `state` is what an earlier session on the same connection saved, so a
    // debugger stays attached, with its breakpoints, across a page reload.
    virtual void attach(ConnectionId connection, SendFn send, const SessionState& state) = 0;
    virtual void detach(ConnectionId connection) = 0;
    virtual void dispatch(ConnectionId connection, const std::string& message) = 0;
};

// Connects DevTools clients to the views' inspectors. Two kinds of client:
//
//  - remote: Chrome DevTools over the local WebSocket server (start/stop);
//  - host: one session per view that the host drives through the C API
//    (sendFromHost/pollHost). The Unity JS console is one; it needs no server.
//
// Messages from clients are queued here and handed to the runtime thread,
// where the inspectors live. While a view is paused at a breakpoint the
// runtime thread waits in waitAndDrain instead of its task loop, so DevTools
// keeps working and the rest of the game keeps running.
class DevToolsHub {
public:
    static DevToolsHub& instance();

    // `post` runs a task on the runtime thread. Without one (the single-threaded
    // runtime) nothing may run from the server thread, and tick() drains.
    void configure(std::function<void(std::function<void()>)> post);

    // --- host's main thread ----------------------------------------------------
    // Starts the local WebSocket endpoint on 127.0.0.1; `port` 0 picks a free
    // one. False when the port cannot be bound.
    bool start(uint16_t port);
    void stop();
    // 0 while the server is not running.
    uint16_t port() const;
    // devtools://... for the view, or empty while the server is not running.
    std::string frontendUrl(TargetId target) const;

    // Host session: a message in, and the responses out.
    void sendFromHost(TargetId target, std::string message);
    bool pollHost(TargetId& target, std::string& message);

    // Stops the server and releases a runtime thread waiting at a breakpoint.
    void shutdown();

    // --- runtime thread --------------------------------------------------------
    void addTarget(TargetId id, IInspectorTarget* target, std::string title, std::string url);
    // The JavaScript runtime went away (reload or destroy). Connections stay,
    // with `states`, until the view gets a runtime again or forgetTarget.
    void suspendTarget(TargetId id, std::vector<std::pair<ConnectionId, SessionState>> states);
    // The view is gone: close its connections and drop what is queued for it.
    void forgetTarget(TargetId id);
    // Handles what is queued for one target, or for every target.
    void drain(TargetId id);
    // Only the attaches queued for `id`: the sessions that outlived the previous
    // document. The inspector calls it as soon as the new context exists, so a
    // breakpoint on the new document's first script line still stops it.
    void attachPending(TargetId id);
    void drainAll();
    // The pause loop: waits for work for `id` and handles it. Returns false when
    // the hub shuts down, which has to end the pause. `done` is re-checked each
    // time something arrives.
    bool waitAndDrain(TargetId id, const std::function<bool()>& done);
    // True for the single-threaded runtime, whose tick has to call drainAll.
    bool drainsOnTick() const;

    // --- server thread (and the tests) ------------------------------------------
    std::string httpResponse(const std::string& path, int& status) const;
    // True when `path` (/devtools/page/<id>) names a view that can be debugged.
    bool hasTarget(const std::string& path) const;
    bool openConnection(ConnectionId connection, const std::string& path);
    void receive(ConnectionId connection, std::string message);
    void closeConnection(ConnectionId connection);

private:
    DevToolsHub();
    ~DevToolsHub();

    enum class EventKind { Attach, Message, Detach };
    struct Event {
        EventKind kind = EventKind::Message;
        TargetId target = 0;
        ConnectionId connection = 0;
        std::string text;
        SessionState state;
    };
    struct Connection {
        TargetId target = 0;
        bool host = false;
    };
    struct Target {
        IInspectorTarget* inspector = nullptr; // null while suspended
        std::string title;
        std::string url;
    };

    void push(Event event, bool front = false); // mutex_ held by the caller
    void requestDrain(TargetId id);
    std::vector<Event> takeEvents(TargetId id); // mutex_ held by the caller
    void handle(TargetId id, std::vector<Event>& events);
    SendFn senderFor(ConnectionId connection, bool host);
    std::string targetUrl(TargetId id, const char* page) const; // mutex_ held

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::function<void(std::function<void()>)> post_;
    std::unique_ptr<DevToolsServer> server_;
    uint16_t port_ = 0;
    bool stopping_ = false;
    std::deque<Event> events_;
    std::map<TargetId, Target> targets_;
    std::map<ConnectionId, Connection> connections_;
    std::map<TargetId, ConnectionId> hostConnections_;
    std::map<ConnectionId, SessionState> suspendedStates_;
    std::deque<std::pair<TargetId, std::string>> hostOutbox_;
    ConnectionId nextHostConnection_ = 1ull << 63; // server connections count up from 1
};

} // namespace xgu::devtools
