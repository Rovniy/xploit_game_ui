#include "devtools/DevToolsServer.h"

#include "core/Log.h"
#include "devtools/WebSocket.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace xgu::devtools {
namespace {

constexpr size_t kMaxHttpHeaderBytes = 16 * 1024;

std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

// Only the loopback names may appear in Host; anything else is a page in a
// browser that resolved its own domain to 127.0.0.1.
bool isLoopbackHost(std::string_view host, uint16_t port) {
    const std::string value = lowered(trim(host));
    const std::string suffix = ":" + std::to_string(port);
    for (const char* name : {"127.0.0.1", "localhost", "[::1]"}) {
        if (value == name || value == std::string(name) + suffix) {
            return true;
        }
    }
    return false;
}

const char* statusText(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    default:
        return "Error";
    }
}

} // namespace

struct DevToolsServer::Client {
    ConnectionId id = 0;
    SOCKET socket = INVALID_SOCKET;
    std::mutex sendMutex;
    std::string http;
    FrameDecoder frames;
    std::atomic<bool> webSocket{false};
    std::atomic<bool> closing{false};
};

DevToolsServer::DevToolsServer(DevToolsHub& hub) : hub_(hub), listenSocket_(INVALID_SOCKET) {}

DevToolsServer::~DevToolsServer() { stop(); }

bool DevToolsServer::start(uint16_t port) {
    if (running()) {
        return true;
    }
    if (!winsockStarted_) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            XGU_LOG_ERROR("DevTools: WSAStartup failed");
            return false;
        }
        winsockStarted_ = true;
    }
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        XGU_LOG_ERROR("DevTools: cannot create a socket (%d)", WSAGetLastError());
        return false;
    }
    // Refuse to share the port with another listener (a browser started with
    // --remote-debugging-port, say) instead of silently splitting connections.
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        ::listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        XGU_LOG_ERROR("DevTools: cannot listen on 127.0.0.1:%u (%d); is the port taken?", static_cast<unsigned>(port),
                      WSAGetLastError());
        closesocket(listener);
        return false;
    }
    sockaddr_in bound{};
    int length = sizeof(bound);
    getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &length);
    port_ = ntohs(bound.sin_port);
    listenSocket_ = static_cast<uintptr_t>(listener);
    stopRequested_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { loop(); });
    return true;
}

void DevToolsServer::stop() {
    if (!running()) {
        return;
    }
    stopRequested_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_release);
    port_ = 0;
    if (winsockStarted_) {
        WSACleanup();
        winsockStarted_ = false;
    }
}

std::shared_ptr<DevToolsServer::Client> DevToolsServer::find(ConnectionId connection) {
    std::lock_guard lock(clientsMutex_);
    const auto it = clients_.find(connection);
    return it == clients_.end() ? nullptr : it->second;
}

void DevToolsServer::writeRaw(Client& client, std::string_view bytes) {
    std::lock_guard lock(client.sendMutex);
    if (client.closing.load(std::memory_order_acquire)) {
        return;
    }
    while (!bytes.empty()) {
        const int chunk = static_cast<int>(std::min<size_t>(bytes.size(), 1u << 20));
        const int sent = ::send(client.socket, bytes.data(), chunk, 0);
        if (sent == SOCKET_ERROR || sent <= 0) {
            // The reader notices the dead socket and cleans up.
            client.closing.store(true, std::memory_order_release);
            shutdown(client.socket, SD_BOTH);
            return;
        }
        bytes.remove_prefix(static_cast<size_t>(sent));
    }
}

void DevToolsServer::send(ConnectionId connection, std::string_view text) {
    if (const std::shared_ptr<Client> client = find(connection); client && client->webSocket) {
        writeRaw(*client, encodeFrame(Opcode::Text, text));
    }
}

void DevToolsServer::close(ConnectionId connection) {
    if (const std::shared_ptr<Client> client = find(connection)) {
        writeRaw(*client, encodeFrame(Opcode::Close, {}));
        client->closing.store(true, std::memory_order_release);
        shutdown(client->socket, SD_BOTH); // wakes the reader, which drops the client
    }
}

void DevToolsServer::loop() {
    const auto listener = static_cast<SOCKET>(listenSocket_);
    std::vector<char> buffer(64 * 1024);
    while (!stopRequested_.load(std::memory_order_acquire)) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        std::vector<std::shared_ptr<Client>> snapshot;
        {
            std::lock_guard lock(clientsMutex_);
            for (const auto& [id, client] : clients_) {
                if (snapshot.size() + 1 >= FD_SETSIZE) {
                    break;
                }
                snapshot.push_back(client);
                FD_SET(client->socket, &readable);
            }
        }
        // A short timeout keeps stop() responsive without a wake-up socket.
        timeval timeout{0, 100 * 1000};
        const int ready = select(0, &readable, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            continue;
        }
        if (FD_ISSET(listener, &readable)) {
            SOCKET accepted = accept(listener, nullptr, nullptr);
            if (accepted != INVALID_SOCKET) {
                BOOL noDelay = TRUE;
                setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
                auto client = std::make_shared<Client>();
                client->socket = accepted;
                std::lock_guard lock(clientsMutex_);
                client->id = nextConnection_++;
                clients_[client->id] = client;
            }
        }
        for (const std::shared_ptr<Client>& client : snapshot) {
            if (!FD_ISSET(client->socket, &readable)) {
                continue;
            }
            const int received = recv(client->socket, buffer.data(), static_cast<int>(buffer.size()), 0);
            bool keep = received > 0;
            if (keep) {
                if (client->webSocket) {
                    client->frames.append(buffer.data(), static_cast<size_t>(received));
                } else {
                    client->http.append(buffer.data(), static_cast<size_t>(received));
                }
                keep = onReadable(*client);
            }
            if (!keep) {
                {
                    std::lock_guard lock(client->sendMutex);
                    client->closing.store(true, std::memory_order_release);
                    closesocket(client->socket);
                }
                {
                    std::lock_guard lock(clientsMutex_);
                    clients_.erase(client->id);
                }
                if (client->webSocket) {
                    hub_.closeConnection(client->id);
                }
            }
        }
    }

    // Shutting down: every client goes, and the hub hears about each session.
    std::map<ConnectionId, std::shared_ptr<Client>> remaining;
    {
        std::lock_guard lock(clientsMutex_);
        remaining.swap(clients_);
    }
    for (auto& [id, client] : remaining) {
        {
            std::lock_guard lock(client->sendMutex);
            client->closing.store(true, std::memory_order_release);
            closesocket(client->socket);
        }
        if (client->webSocket) {
            hub_.closeConnection(id);
        }
    }
    closesocket(listener);
    listenSocket_ = INVALID_SOCKET;
}

bool DevToolsServer::onReadable(Client& client) {
    if (!client.webSocket) {
        return handleHttp(client);
    }
    return handleFrames(client);
}

bool DevToolsServer::handleHttp(Client& client) {
    const size_t end = client.http.find("\r\n\r\n");
    if (end == std::string::npos) {
        return client.http.size() <= kMaxHttpHeaderBytes;
    }
    const std::string head = client.http.substr(0, end);
    std::string rest = client.http.substr(end + 4);
    client.http.clear();

    // Request line: GET <path> HTTP/1.1
    const size_t lineEnd = head.find("\r\n");
    const std::string requestLine = head.substr(0, lineEnd);
    const size_t firstSpace = requestLine.find(' ');
    const size_t secondSpace = requestLine.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
        return false;
    }
    const std::string method = requestLine.substr(0, firstSpace);
    std::string path = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    if (const size_t query = path.find('?'); query != std::string::npos) {
        path.resize(query);
    }

    std::map<std::string, std::string> headers;
    size_t position = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
    while (position < head.size()) {
        size_t next = head.find("\r\n", position);
        if (next == std::string::npos) {
            next = head.size();
        }
        const std::string_view line(head.data() + position, next - position);
        if (const size_t colon = line.find(':'); colon != std::string_view::npos) {
            headers[lowered(trim(line.substr(0, colon)))] = std::string(trim(line.substr(colon + 1)));
        }
        position = next + 2;
    }

    const auto respond = [&](int status, const std::string& body) {
        std::string response = "HTTP/1.1 " + std::to_string(status) + " " + statusText(status) +
                               "\r\nContent-Type: application/json; charset=UTF-8\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        writeRaw(client, response);
    };

    if (method != "GET") {
        respond(400, "{}");
        return false;
    }
    if (!isLoopbackHost(headers["host"], port_)) {
        respond(403, "{\"error\":\"Host must be 127.0.0.1 or localhost\"}");
        return false;
    }

    const bool upgrade = lowered(headers["upgrade"]) == "websocket";
    if (!upgrade) {
        int status = 404;
        const std::string body = hub_.httpResponse(path, status);
        respond(status, body);
        return false; // Connection: close
    }

    const std::string key = headers["sec-websocket-key"];
    if (key.empty() || !hub_.hasTarget(path)) {
        respond(404, "{\"error\":\"no such target\"}");
        return false;
    }
    writeRaw(client, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: " +
                         webSocketAccept(key) + "\r\n\r\n");
    // Only now may the session talk: its first message has to follow the 101.
    client.webSocket.store(true, std::memory_order_release);
    if (!hub_.openConnection(client.id, path)) {
        return false; // the view went away in between
    }
    if (!rest.empty()) {
        client.frames.append(rest.data(), rest.size());
        return handleFrames(client);
    }
    return true;
}

bool DevToolsServer::handleFrames(Client& client) {
    Opcode opcode = Opcode::Text;
    std::string payload;
    for (;;) {
        switch (client.frames.next(opcode, payload)) {
        case FrameDecoder::Result::NeedMore:
            return true;
        case FrameDecoder::Result::Error:
            return false;
        case FrameDecoder::Result::Message:
            break;
        }
        switch (opcode) {
        case Opcode::Text:
            hub_.receive(client.id, std::move(payload));
            break;
        case Opcode::Ping:
            writeRaw(client, encodeFrame(Opcode::Pong, payload));
            break;
        case Opcode::Close:
            writeRaw(client, encodeFrame(Opcode::Close, {}));
            return false;
        default:
            break; // binary and pong frames carry nothing for us
        }
    }
}

} // namespace xgu::devtools
