#include "devtools/WebSocket.h"

#include <array>

namespace xgu::devtools {
namespace {

uint32_t rotateLeft(uint32_t value, int bits) { return (value << bits) | (value >> (32 - bits)); }

} // namespace

std::string sha1(std::string_view data) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    std::string message(data);
    const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8u;
    message.push_back(static_cast<char>(0x80));
    while (message.size() % 64 != 56) {
        message.push_back('\0');
    }
    for (int i = 7; i >= 0; --i) {
        message.push_back(static_cast<char>((bitLength >> (i * 8)) & 0xFFu));
    }

    for (size_t chunk = 0; chunk < message.size(); chunk += 64) {
        std::array<uint32_t, 80> w{};
        for (size_t i = 0; i < 16; ++i) {
            const auto byte = [&](size_t k) { return static_cast<uint32_t>(static_cast<uint8_t>(message[chunk + k])); };
            w[i] = (byte(i * 4) << 24) | (byte(i * 4 + 1) << 16) | (byte(i * 4 + 2) << 8) | byte(i * 4 + 3);
        }
        for (size_t i = 16; i < 80; ++i) {
            w[i] = rotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (size_t i = 0; i < 80; ++i) {
            uint32_t f = 0;
            uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const uint32_t temp = rotateLeft(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    std::string digest;
    for (uint32_t word : h) {
        for (int i = 3; i >= 0; --i) {
            digest.push_back(static_cast<char>((word >> (i * 8)) & 0xFFu));
        }
    }
    return digest;
}

std::string base64Encode(std::string_view data) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const uint32_t n = (static_cast<uint8_t>(data[i]) << 16) | (static_cast<uint8_t>(data[i + 1]) << 8) |
                           static_cast<uint8_t>(data[i + 2]);
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(kAlphabet[(n >> 6) & 63]);
        out.push_back(kAlphabet[n & 63]);
    }
    if (i < data.size()) {
        uint32_t n = static_cast<uint8_t>(data[i]) << 16;
        const bool two = i + 1 < data.size();
        if (two) {
            n |= static_cast<uint8_t>(data[i + 1]) << 8;
        }
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(two ? kAlphabet[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

std::string webSocketAccept(std::string_view clientKey) {
    std::string input(clientKey);
    input += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    return base64Encode(sha1(input));
}

namespace {

void appendHeader(std::string& out, Opcode opcode, size_t length, bool masked, bool fin) {
    out.push_back(static_cast<char>((fin ? 0x80 : 0x00) | static_cast<uint8_t>(opcode)));
    const uint8_t maskBit = masked ? 0x80 : 0x00;
    if (length < 126) {
        out.push_back(static_cast<char>(maskBit | length));
    } else if (length <= 0xFFFF) {
        out.push_back(static_cast<char>(maskBit | 126));
        out.push_back(static_cast<char>((length >> 8) & 0xFF));
        out.push_back(static_cast<char>(length & 0xFF));
    } else {
        out.push_back(static_cast<char>(maskBit | 127));
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<char>((static_cast<uint64_t>(length) >> (i * 8)) & 0xFF));
        }
    }
}

bool isControl(Opcode opcode) { return (static_cast<uint8_t>(opcode) & 0x8) != 0; }

} // namespace

std::string encodeFrame(Opcode opcode, std::string_view payload) {
    std::string out;
    out.reserve(payload.size() + 10);
    appendHeader(out, opcode, payload.size(), false, true);
    out.append(payload);
    return out;
}

std::string encodeClientFrame(Opcode opcode, std::string_view payload, uint32_t mask, bool fin) {
    std::string out;
    appendHeader(out, opcode, payload.size(), true, fin);
    const char key[4] = {static_cast<char>(mask >> 24), static_cast<char>(mask >> 16), static_cast<char>(mask >> 8),
                         static_cast<char>(mask)};
    out.append(key, 4);
    for (size_t i = 0; i < payload.size(); ++i) {
        out.push_back(static_cast<char>(payload[i] ^ key[i % 4]));
    }
    return out;
}

FrameDecoder::Result FrameDecoder::next(Opcode& opcode, std::string& payload) {
    for (;;) {
        if (buffer_.size() < 2) {
            return Result::NeedMore;
        }
        const auto byte = [&](size_t i) { return static_cast<uint8_t>(buffer_[i]); };
        const bool fin = (byte(0) & 0x80) != 0;
        if ((byte(0) & 0x70) != 0) {
            return Result::Error; // no extensions were negotiated
        }
        const auto frameOpcode = static_cast<Opcode>(byte(0) & 0x0F);
        const bool masked = (byte(1) & 0x80) != 0;
        if (!masked) {
            return Result::Error; // every client frame is masked
        }
        uint64_t length = byte(1) & 0x7F;
        size_t offset = 2;
        if (length == 126) {
            if (buffer_.size() < 4) {
                return Result::NeedMore;
            }
            length = (static_cast<uint64_t>(byte(2)) << 8) | byte(3);
            offset = 4;
        } else if (length == 127) {
            if (buffer_.size() < 10) {
                return Result::NeedMore;
            }
            length = 0;
            for (size_t i = 0; i < 8; ++i) {
                length = (length << 8) | byte(2 + i);
            }
            offset = 10;
        }
        if (length > maxMessageBytes_ || (isControl(frameOpcode) && (length > 125 || !fin))) {
            return Result::Error;
        }
        if (buffer_.size() < offset + 4 + length) {
            return Result::NeedMore;
        }
        const char* key = buffer_.data() + offset;
        std::string data(buffer_.data() + offset + 4, static_cast<size_t>(length));
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<char>(data[i] ^ key[i % 4]);
        }
        buffer_.erase(0, offset + 4 + static_cast<size_t>(length));

        if (isControl(frameOpcode)) {
            opcode = frameOpcode;
            payload = std::move(data);
            return Result::Message;
        }
        if (frameOpcode == Opcode::Continuation) {
            if (!inFragment_) {
                return Result::Error;
            }
        } else {
            if (inFragment_) {
                return Result::Error; // a new message inside an unfinished one
            }
            fragmentOpcode_ = frameOpcode;
            fragments_.clear();
        }
        if (fragments_.size() + data.size() > maxMessageBytes_) {
            return Result::Error;
        }
        fragments_ += data;
        inFragment_ = !fin;
        if (fin) {
            opcode = fragmentOpcode_;
            payload = std::move(fragments_);
            fragments_.clear();
            return Result::Message;
        }
    }
}

} // namespace xgu::devtools
