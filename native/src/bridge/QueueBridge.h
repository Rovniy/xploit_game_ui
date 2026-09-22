#pragma once

#include "core/interfaces/IBridge.h"

#include <deque>
#include <mutex>

namespace xgu::bridge {

// The default bridge: two bounded queues behind a mutex.
//
// Only Emit messages are ever dropped when a queue is full, and the oldest go
// first: losing a stale notification is better than stalling the page, while a
// Call or a Reply must not vanish or a promise would hang for ever.
class QueueBridge final : public IBridge {
public:
    // Payloads above this are refused with an error in the page's console; a
    // game UI that needs to move more than this should stream it instead.
    static constexpr size_t kMaxPayloadBytes = 4u * 1024u * 1024u;
    // Per direction. Reached only when one side stops pumping.
    static constexpr size_t kMaxQueued = 10000;

    bool postToHost(BridgeMessage message) override;
    void drainToPage(std::vector<BridgeMessage>& out) override;
    bool postToPage(BridgeMessage message) override;
    bool pollFromPage(BridgeMessage& out) override;

    size_t droppedCount() const override;
    size_t maxPayloadBytes() const override { return kMaxPayloadBytes; }

    // Queue depths, for tests and diagnostics.
    size_t pendingForHost() const;
    size_t pendingForPage() const;

    // Drops everything; used when the document is replaced.
    void clear();

private:
    // Makes room for one more message, dropping the oldest droppable one.
    // Returns false when nothing may be dropped and the queue is full.
    static bool makeRoom(std::deque<BridgeMessage>& queue, size_t& dropped);

    mutable std::mutex mutex_;
    std::deque<BridgeMessage> toHost_;
    std::deque<BridgeMessage> toPage_;
    size_t dropped_ = 0;
};

} // namespace xgu::bridge
