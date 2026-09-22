#pragma once

#include "render/DisplayList.h"

#include <atomic>
#include <mutex>
#include <optional>

namespace xgu::render {

// Single-slot, latest-wins handoff from the runtime thread (producer) to the
// submission thread (consumer). Dropping an unconsumed frame is free because
// the picture is ref-counted.
class FrameMailbox {
public:
    void publish(DisplayList frame) {
        std::lock_guard lock(mutex_);
        slot_ = std::move(frame);
        pending_.store(true, std::memory_order_release);
    }

    std::optional<DisplayList> take() {
        std::lock_guard lock(mutex_);
        pending_.store(false, std::memory_order_release);
        std::optional<DisplayList> out = std::move(slot_);
        slot_.reset();
        return out;
    }

    bool hasPending() const { return pending_.load(std::memory_order_acquire); }

    void clear() {
        std::lock_guard lock(mutex_);
        slot_.reset();
        pending_.store(false, std::memory_order_release);
    }

private:
    mutable std::mutex mutex_;
    std::optional<DisplayList> slot_;
    std::atomic<bool> pending_{false};
};

} // namespace xgu::render
