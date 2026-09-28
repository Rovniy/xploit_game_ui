// DevTools: the WebSocket framing, the host session, and Chrome DevTools over
// the local endpoint, including a script stopped at a breakpoint and resumed.

#include "core/Runtime.h"
#include "devtools/WebSocket.h"
#include "render/RenderSystem.h"

#include <xploit_game_ui/xgu.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace xgu::devtools;

namespace {

std::string hex(const std::string& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (const unsigned char c : bytes) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 15]);
    }
    return out;
}

} // namespace

// --- protocol ----------------------------------------------------------------------------

TEST(WebSocket, Sha1AndBase64MatchTheirTestVectors) {
    EXPECT_EQ(hex(sha1("abc")), "a9993e364706816aba3e25717850c26c9cd0d89d");
    EXPECT_EQ(hex(sha1("")), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    EXPECT_EQ(base64Encode("f"), "Zg==");
    EXPECT_EQ(base64Encode("fo"), "Zm8=");
    EXPECT_EQ(base64Encode("foo"), "Zm9v");
}

TEST(WebSocket, AcceptKeyIsTheOneFromRfc6455) {
    EXPECT_EQ(webSocketAccept("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(WebSocket, DecoderUnmasksJoinsFragmentsAndPassesControlFrames) {
    FrameDecoder decoder;
    std::string wire = encodeClientFrame(Opcode::Text, "Hel", 0x11223344u, false);
    wire += encodeClientFrame(Opcode::Ping, "p", 0x01020304u);
    wire += encodeClientFrame(Opcode::Continuation, "lo", 0x55667788u);
    const std::string big(70000, 'x'); // needs the 64-bit length
    wire += encodeClientFrame(Opcode::Text, big, 0xA0B0C0D0u);

    // Byte by byte, the way the network may deliver it.
    std::vector<std::pair<Opcode, std::string>> messages;
    for (const char c : wire) {
        decoder.append(&c, 1);
        Opcode opcode = Opcode::Text;
        std::string payload;
        while (decoder.next(opcode, payload) == FrameDecoder::Result::Message) {
            messages.emplace_back(opcode, payload);
        }
    }
    ASSERT_EQ(messages.size(), 3u);
    EXPECT_EQ(messages[0].first, Opcode::Ping) << "a control frame may come between fragments";
    EXPECT_EQ(messages[1].second, "Hello");
    EXPECT_EQ(messages[2].second, big);
}

TEST(WebSocket, DecoderRejectsUnmaskedClientFrames) {
    FrameDecoder decoder;
    const std::string frame = encodeFrame(Opcode::Text, "hi");
    decoder.append(frame.data(), frame.size());
    Opcode opcode = Opcode::Text;
    std::string payload;
    EXPECT_EQ(decoder.next(opcode, payload), FrameDecoder::Result::Error);
}

// --- sessions -----------------------------------------------------------------------------

namespace {

std::mutex g_logMutex;
std::vector<std::string> g_logs;

void captureLog(void*, int, const char* message) {
    std::lock_guard lock(g_logMutex);
    g_logs.emplace_back(message ? message : "");
}

std::string allLogs() {
    std::lock_guard lock(g_logMutex);
    std::string out;
    for (const std::string& line : g_logs) {
        out += line + '\n';
    }
    return out;
}

bool logged(const std::string& needle) {
    std::lock_guard lock(g_logMutex);
    for (const std::string& line : g_logs) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// A blocking WebSocket client, enough to act as Chrome DevTools.
class Client {
public:
    ~Client() { close(); }

    bool connect(uint16_t port, const std::string& request) {
        socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        DWORD timeout = 5000;
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            return false;
        }
        return sendRaw(request);
    }

    bool sendRaw(const std::string& bytes) {
        return ::send(socket_, bytes.data(), static_cast<int>(bytes.size()), 0) == static_cast<int>(bytes.size());
    }

    // Everything until the server closes (plain HTTP).
    std::string readAll() {
        std::string out;
        char buffer[4096];
        int received = 0;
        while ((received = recv(socket_, buffer, sizeof(buffer), 0)) > 0) {
            out.append(buffer, static_cast<size_t>(received));
        }
        return out;
    }

    bool handshake(uint16_t port, const std::string& path) {
        const std::string request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                                    "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (!connect(port, request)) {
            return false;
        }
        while (pending_.find("\r\n\r\n") == std::string::npos) {
            if (!fill()) {
                return false;
            }
        }
        const size_t end = pending_.find("\r\n\r\n");
        const std::string head = pending_.substr(0, end);
        pending_.erase(0, end + 4);
        return head.find(" 101 ") != std::string::npos &&
               head.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos;
    }

    bool sendText(const std::string& text) { return sendRaw(encodeClientFrame(Opcode::Text, text, 0x12345678u)); }

    // One text message from the server; empty on timeout or close.
    std::string readMessage() {
        for (;;) {
            if (pending_.size() >= 2) {
                const auto byte = [&](size_t i) { return static_cast<uint8_t>(pending_[i]); };
                uint64_t length = byte(1) & 0x7F;
                size_t offset = 2;
                if (length == 126 && pending_.size() >= 4) {
                    length = (static_cast<uint64_t>(byte(2)) << 8) | byte(3);
                    offset = 4;
                } else if (length == 127 && pending_.size() >= 10) {
                    length = 0;
                    for (size_t i = 0; i < 8; ++i) {
                        length = (length << 8) | byte(2 + i);
                    }
                    offset = 10;
                } else if (length >= 126) {
                    offset = 0;
                }
                if (offset > 0 && pending_.size() >= offset + length) {
                    const auto opcode = static_cast<Opcode>(byte(0) & 0x0F);
                    std::string payload = pending_.substr(offset, static_cast<size_t>(length));
                    pending_.erase(0, offset + static_cast<size_t>(length));
                    if (opcode == Opcode::Text) {
                        return payload;
                    }
                    continue;
                }
            }
            if (!fill()) {
                return {};
            }
        }
    }

    // Reads until a message containing `needle` arrives.
    std::string waitFor(const std::string& needle) {
        for (int i = 0; i < 200; ++i) {
            std::string message = readMessage();
            if (message.empty()) {
                return {};
            }
            if (message.find(needle) != std::string::npos) {
                return message;
            }
        }
        return {};
    }

    void close() {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

private:
    bool fill() {
        char buffer[8192];
        const int received = recv(socket_, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return false;
        }
        pending_.append(buffer, static_cast<size_t>(received));
        return true;
    }

    SOCKET socket_ = INVALID_SOCKET;
    std::string pending_;
};

class DevToolsTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    static void TearDownTestSuite() { WSACleanup(); }

    void SetUp() override {
        {
            std::lock_guard lock(g_logMutex);
            g_logs.clear();
        }
        xgu_init_desc init{};
        init.struct_size = sizeof(init);
        init.log_fn = &captureLog;
        init.flags = XGU_INIT_SINGLE_THREADED;
        ASSERT_EQ(xgu_initialize(&init), XGU_OK);
        xgu_set_log_callback(&captureLog, nullptr);
        xgu::Runtime::instance().render().setNoDevice();

        xgu_view_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = 16;
        desc.height = 16;
        desc.device_pixel_ratio = 1.0f;
        desc.format = XGU_FORMAT_RGBA8;
        desc.provider = XGU_PROVIDER_CPU;
        desc.name = "devtools-test";
        view_ = xgu_view_create(&desc);
        ASSERT_NE(view_, XGU_INVALID_VIEW);
        ASSERT_EQ(xgu_view_load_html(view_, "<html><body><script>var answer = 42;</script></body></html>", nullptr),
                  XGU_OK);
        drainHost();
    }

    void TearDown() override {
        xgu_devtools_stop();
        xgu_views_destroy_all();
        xgu_tick(0.0);
        drainHost();
        xgu_set_log_callback(nullptr, nullptr);
    }

    static void drainHost() {
        xgu_view_id view = 0;
        const char* message = nullptr;
        while (xgu_devtools_poll(&view, &message)) {
        }
    }

    // The next host-session message that contains `needle`.
    std::string hostWaitFor(const std::string& needle) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            xgu_tick(0.0);
            xgu_view_id view = 0;
            const char* message = nullptr;
            while (xgu_devtools_poll(&view, &message)) {
                const std::string text = message;
                if (view == view_ && text.find(needle) != std::string::npos) {
                    return text;
                }
            }
        }
        return {};
    }

    // The single-threaded runtime handles what the server received on tick;
    // give the server thread a moment to receive it first.
    static void pump() {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            xgu_tick(0.0);
        }
    }

    std::string path() const { return "/devtools/page/" + std::to_string(view_); }

    xgu_view_id view_ = XGU_INVALID_VIEW;
};

} // namespace

TEST_F(DevToolsTest, HostSessionEvaluatesInThePage) {
    ASSERT_EQ(xgu_view_devtools_send(
                  view_, R"({"id":1,"method":"Runtime.evaluate","params":{"expression":"answer + 1","replMode":true}})"),
              XGU_OK);
    const std::string response = hostWaitFor("\"id\":1");
    EXPECT_NE(response.find("\"value\":43"), std::string::npos) << response;
}

TEST_F(DevToolsTest, ConsoleStillLogsAndTheRestOfTheBuiltInConsoleWorks) {
    ASSERT_EQ(xgu_view_execute_js(view_, "console.log({ a: 1 }); console.count('clicks');", "console.js"), XGU_OK);
    EXPECT_TRUE(logged("{\"a\":1}")) << "the wrapped methods keep their own format";
    EXPECT_TRUE(logged("clicks: 1")) << "console.count reaches the log through the inspector";
}

TEST_F(DevToolsTest, HostSessionSurvivesAReload) {
    ASSERT_EQ(xgu_view_devtools_send(view_, R"({"id":1,"method":"Runtime.enable"})"), XGU_OK);
    ASSERT_FALSE(hostWaitFor("\"id\":1").empty());

    ASSERT_EQ(xgu_view_load_html(view_, "<html><body><script>var answer = 7;</script></body></html>", nullptr),
              XGU_OK);
    // The restored session reports the new document's context by itself.
    EXPECT_FALSE(hostWaitFor("Runtime.executionContextCreated").empty());
    ASSERT_EQ(xgu_view_devtools_send(view_, R"({"id":2,"method":"Runtime.evaluate","params":{"expression":"answer"}})"),
              XGU_OK);
    EXPECT_NE(hostWaitFor("\"id\":2").find("\"value\":7"), std::string::npos);
}

TEST_F(DevToolsTest, DiscoveryListsTheViewAndRefusesForeignHosts) {
    ASSERT_EQ(xgu_devtools_start(0), XGU_OK);
    const uint16_t port = xgu_devtools_port();
    ASSERT_NE(port, 0);

    Client list;
    ASSERT_TRUE(list.connect(port, "GET /json/list HTTP/1.1\r\nHost: localhost:" + std::to_string(port) + "\r\n\r\n"));
    const std::string body = list.readAll();
    EXPECT_NE(body.find(" 200 "), std::string::npos) << body;
    EXPECT_NE(body.find("\"title\":\"devtools-test\""), std::string::npos) << body;
    EXPECT_NE(body.find("ws://127.0.0.1:" + std::to_string(port) + path()), std::string::npos) << body;

    Client foreign;
    ASSERT_TRUE(foreign.connect(port, "GET /json/list HTTP/1.1\r\nHost: attacker.example\r\n\r\n"));
    EXPECT_NE(foreign.readAll().find(" 403 "), std::string::npos);

    char url[256] = {};
    EXPECT_GT(xgu_view_devtools_url(view_, url, sizeof(url)), 0u);
    EXPECT_NE(std::string(url).find("js_app.html?"), std::string::npos) << url;
}

TEST_F(DevToolsTest, ChromeDevToolsCanEvaluateAndStopAtABreakpoint) {
    ASSERT_EQ(xgu_devtools_start(0), XGU_OK);
    const uint16_t port = xgu_devtools_port();

    Client devtools;
    ASSERT_TRUE(devtools.handshake(port, path()));
    ASSERT_TRUE(devtools.sendText(R"({"id":1,"method":"Runtime.evaluate","params":{"expression":"answer * 2"}})"));
    pump();
    const std::string response = devtools.waitFor("\"id\":1");
    EXPECT_NE(response.find("\"value\":84"), std::string::npos) << response;

    ASSERT_TRUE(devtools.sendText(R"({"id":2,"method":"Debugger.enable"})"));
    pump();
    ASSERT_FALSE(devtools.waitFor("\"id\":2").empty());

    // A helper plays the person at the keyboard: when the script stops, resume.
    bool sawPause = false;
    std::thread person([&] {
        sawPause = !devtools.waitFor("Debugger.paused").empty();
        devtools.sendText(R"({"id":3,"method":"Debugger.resume"})");
    });
    // Runs on this thread, stops at `debugger`, and returns once resumed.
    ASSERT_EQ(xgu_view_execute_js(view_, "debugger; globalThis.after = 1;", "pause.js"), XGU_OK);
    person.join();
    EXPECT_TRUE(sawPause);

    ASSERT_EQ(xgu_view_devtools_send(view_, R"({"id":9,"method":"Runtime.evaluate","params":{"expression":"after"}})"),
              XGU_OK);
    EXPECT_NE(hostWaitFor("\"id\":9").find("\"value\":1"), std::string::npos) << "the script ran on after resuming";
}

TEST_F(DevToolsTest, BreakpointsSurviveAReloadAndStopTheNewDocument) {
    ASSERT_EQ(xgu_view_devtools_send(view_, R"({"id":1,"method":"Runtime.evaluate","params":{"expression":"1"}})"),
              XGU_OK); // the view has a runtime
    ASSERT_EQ(xgu_devtools_start(0), XGU_OK);
    Client devtools;
    ASSERT_TRUE(devtools.handshake(xgu_devtools_port(), path()));
    ASSERT_TRUE(devtools.sendText(R"({"id":1,"method":"Debugger.enable"})"));
    ASSERT_TRUE(devtools.sendText(
        R"({"id":2,"method":"Debugger.setBreakpointByUrl","params":{"urlRegex":"inline","lineNumber":0}})"));
    pump();
    ASSERT_FALSE(devtools.waitFor("\"id\":2").empty());

    bool sawPause = false;
    std::thread person([&] {
        sawPause = !devtools.waitFor("Debugger.paused").empty();
        devtools.sendText(R"({"id":3,"method":"Debugger.resume"})");
    });
    // The reload runs the new document's script on this thread; the restored
    // session still holds the breakpoint, so it stops on the first line.
    ASSERT_EQ(xgu_view_load_html(view_, "<html><body><script>var reloaded = true;</script></body></html>", "page.html"),
              XGU_OK);
    person.join();
    EXPECT_TRUE(sawPause) << "the breakpoint set before the reload stopped the new document";
}

TEST_F(DevToolsTest, ARejectionHandledLaterInTheSameTurnIsNotReported) {
    // The handler arrives after the rejection, but before the microtasks end.
    ASSERT_EQ(xgu_view_execute_js(view_,
                                  "const p = Promise.reject(new Error('late')); p.catch(function () {});"
                                  "Promise.reject(new Error('nobody'));",
                                  "rejections.js"),
              XGU_OK);
    EXPECT_FALSE(logged("Error: late")) << "handled within the same turn, but the log says:\n" << allLogs();
    EXPECT_TRUE(logged("Unhandled promise rejection: Error: nobody"));
}

TEST_F(DevToolsTest, AnErrorEvaluatedFromTheConsoleIsNotAnUnhandledRejection) {
    // replMode + awaitPromise: the console awaits the result, so an error is the
    // answer to the request, not something nobody handled.
    ASSERT_EQ(xgu_view_devtools_send(view_, R"({"id":4,"method":"Runtime.evaluate","params":{"expression":"missingName.value","replMode":true,"awaitPromise":true}})"),
              XGU_OK);
    EXPECT_NE(hostWaitFor("\"id\":4").find("ReferenceError"), std::string::npos);
    EXPECT_FALSE(logged("Unhandled promise rejection"));
}
