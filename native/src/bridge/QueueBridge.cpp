#include "bridge/QueueBridge.h"

#include "core/Log.h"

namespace xgu::bridge {

bool QueueBridge::makeRoom(std::deque<BridgeMessage>& queue, size_t& dropped) {
    if (queue.size() < kMaxQueued) {
        return true;
    }
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        if (it->kind == BridgeMessageKind::Emit || it->kind == BridgeMessageKind::Send) {
            queue.erase(it);
            ++dropped;
            return true;
        }
    }
    return false; // nothing but calls and replies: refuse rather than lose one
}

bool QueueBridge::postToHost(BridgeMessage message) {
    if (message.json.size() > kMaxPayloadBytes) {
        XGU_LOG_ERROR("bridge: \"%s\" carries %zu bytes, over the %zu byte limit", message.name.c_str(),
                      message.json.size(), kMaxPayloadBytes);
        return false;
    }
    std::lock_guard lock(mutex_);
    if (!makeRoom(toHost_, dropped_)) {
        XGU_LOG_ERROR("bridge: the queue to the host is full of calls; \"%s\" was refused", message.name.c_str());
        return false;
    }
    toHost_.push_back(std::move(message));
    return true;
}

bool QueueBridge::postToPage(BridgeMessage message) {
    if (message.json.size() > kMaxPayloadBytes) {
        XGU_LOG_ERROR("bridge: \"%s\" carries %zu bytes, over the %zu byte limit", message.name.c_str(),
                      message.json.size(), kMaxPayloadBytes);
        return false;
    }
    std::lock_guard lock(mutex_);
    if (!makeRoom(toPage_, dropped_)) {
        XGU_LOG_ERROR("bridge: the queue to the page is full of replies; \"%s\" was refused", message.name.c_str());
        return false;
    }
    toPage_.push_back(std::move(message));
    return true;
}

void QueueBridge::drainToPage(std::vector<BridgeMessage>& out) {
    std::lock_guard lock(mutex_);
    out.reserve(out.size() + toPage_.size());
    while (!toPage_.empty()) {
        out.push_back(std::move(toPage_.front()));
        toPage_.pop_front();
    }
}

bool QueueBridge::pollFromPage(BridgeMessage& out) {
    std::lock_guard lock(mutex_);
    if (toHost_.empty()) {
        return false;
    }
    out = std::move(toHost_.front());
    toHost_.pop_front();
    return true;
}

size_t QueueBridge::droppedCount() const {
    std::lock_guard lock(mutex_);
    return dropped_;
}

size_t QueueBridge::pendingForHost() const {
    std::lock_guard lock(mutex_);
    return toHost_.size();
}

size_t QueueBridge::pendingForPage() const {
    std::lock_guard lock(mutex_);
    return toPage_.size();
}

void QueueBridge::clear() {
    std::lock_guard lock(mutex_);
    toHost_.clear();
    toPage_.clear();
}

} // namespace xgu::bridge
