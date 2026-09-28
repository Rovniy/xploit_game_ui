#pragma once

#include "devtools/DevToolsHub.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace xgu::devtools {

// A minimal HTTP + WebSocket server for the Chrome DevTools protocol, bound to
// 127.0.0.1 only. It answers the discovery endpoints (/json, /json/list,
// /json/version) and upgrades /devtools/page/<id> to a WebSocket. Requests
// whose Host header is not the loopback address are refused, so a web page in
// a browser cannot reach it through DNS rebinding.
//
// One thread accepts, reads and parses; sending may happen from any thread.
class DevToolsServer {
public:
    explicit DevToolsServer(DevToolsHub& hub);
    ~DevToolsServer();

    // `port` 0 picks a free one. False when the socket cannot be bound.
    bool start(uint16_t port);
    void stop();
    bool running() const { return running_.load(std::memory_order_acquire); }
    uint16_t port() const { return port_; }

    // Any thread. Quietly does nothing for a connection that is gone.
    void send(ConnectionId connection, std::string_view text);
    void close(ConnectionId connection);

private:
    struct Client;

    void loop();
    // False when the client has to be dropped.
    bool onReadable(Client& client);
    bool handleHttp(Client& client);
    bool handleFrames(Client& client);
    void writeRaw(Client& client, std::string_view bytes);
    std::shared_ptr<Client> find(ConnectionId connection);

    DevToolsHub& hub_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    uint16_t port_ = 0;
    uintptr_t listenSocket_;
    std::thread thread_;
    std::mutex clientsMutex_;
    std::map<ConnectionId, std::shared_ptr<Client>> clients_;
    ConnectionId nextConnection_ = 1;
    bool winsockStarted_ = false;
};

} // namespace xgu::devtools
