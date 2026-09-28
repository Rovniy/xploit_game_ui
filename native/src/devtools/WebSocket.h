#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The small part of RFC 6455 a local DevTools endpoint needs: the handshake key
// and text frames in both directions. No extensions, no subprotocols.
namespace xgu::devtools {

// SHA-1 digest (20 bytes) of `data`. Only the handshake uses it.
std::string sha1(std::string_view data);

std::string base64Encode(std::string_view data);

// Sec-WebSocket-Accept for a client's Sec-WebSocket-Key.
std::string webSocketAccept(std::string_view clientKey);

enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

// One unmasked frame from the server side, FIN set.
std::string encodeFrame(Opcode opcode, std::string_view payload);

// A masked frame the way a client sends one; the tests use it.
std::string encodeClientFrame(Opcode opcode, std::string_view payload, uint32_t mask, bool fin = true);

// Takes bytes as they arrive and hands out whole messages, joining fragments
// and unmasking. Control frames come out on their own, even between fragments.
class FrameDecoder {
public:
    enum class Result {
        NeedMore, // not a whole frame yet
        Message,  // `opcode` and `payload` hold one message
        Error,    // protocol violation: close the connection
    };

    explicit FrameDecoder(size_t maxMessageBytes = 256u * 1024u * 1024u) : maxMessageBytes_(maxMessageBytes) {}

    void append(const char* data, size_t size) { buffer_.append(data, size); }

    // Call until it stops returning Message.
    Result next(Opcode& opcode, std::string& payload);

private:
    std::string buffer_;
    std::string fragments_;
    Opcode fragmentOpcode_ = Opcode::Text;
    bool inFragment_ = false;
    size_t maxMessageBytes_;
};

} // namespace xgu::devtools
