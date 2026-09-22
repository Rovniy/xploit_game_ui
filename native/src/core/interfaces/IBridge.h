#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xgu {

// What a bridge message is for. The engine never looks inside the payload: the
// page speaks JSON through v8::JSON and the host parses it with its own reader,
// so no JSON library is needed in the runtime.
enum class BridgeMessageKind : uint8_t {
    Emit,  // page -> host: Unity.emit(name, ...args)
    Send,  // host -> page: view.Send(name, ...args)
    Call,  // page -> host: Unity.call(name, ...args); expects a Reply
    Reply, // host -> page: the result of a Call
};

struct BridgeMessage {
    BridgeMessageKind kind = BridgeMessageKind::Emit;
    // Correlates a Call with its Reply; zero for Emit and Send.
    uint64_t id = 0;
    // Reply only: false means `json` holds {name, message, stack} and the page's
    // promise is rejected.
    bool ok = true;
    std::string name;
    // A JSON array of arguments for Emit, Send and Call; a single JSON value for
    // Reply. Empty means "no arguments".
    std::string json;
};

// Carries messages between the page (runtime thread) and the host (Unity's main
// thread). One of the eight replaceable interfaces: swapping the transport, for
// example for an out-of-process host, means replacing only this.
class IBridge {
public:
    virtual ~IBridge() = default;

    // --- page side, runtime thread -------------------------------------------
    // Queues a message for the host. Returns false when it was rejected (too
    // large, or the queue is full of messages that may not be dropped).
    virtual bool postToHost(BridgeMessage message) = 0;
    // Takes everything the host queued for the page.
    virtual void drainToPage(std::vector<BridgeMessage>& out) = 0;

    // --- host side, main thread ----------------------------------------------
    virtual bool postToPage(BridgeMessage message) = 0;
    // Takes the next message for the host; false when there is none.
    virtual bool pollFromPage(BridgeMessage& out) = 0;

    // Messages dropped because a queue was full, for diagnostics.
    virtual size_t droppedCount() const = 0;
    // Largest payload accepted, in bytes.
    virtual size_t maxPayloadBytes() const = 0;
};

} // namespace xgu
