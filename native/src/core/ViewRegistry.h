#pragma once

#include "core/View.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace xgu {

using ViewId = uint64_t;

// Slot table with generation counters. Ids are {index:32, generation:32}; a
// destroyed slot bumps its generation so stale ids resolve to nullptr.
class ViewRegistry {
public:
    static constexpr ViewId kInvalid = 0;

    ViewId add(std::unique_ptr<View> view);
    std::unique_ptr<View> remove(ViewId id);
    std::vector<std::unique_ptr<View>> removeAll();

    // Returns nullptr for stale/unknown ids. The pointer stays valid only while
    // the caller holds the registry mutex (see withView) or owns the view.
    View* resolve(ViewId id) const;

    // Runs `fn(view)` under the registry lock; returns false for stale ids.
    bool withView(ViewId id, const std::function<void(View&)>& fn);
    void forEach(const std::function<void(ViewId, View&)>& fn);

    size_t size() const;

    static uint32_t indexOf(ViewId id) { return static_cast<uint32_t>(id & 0xFFFFFFFFu); }
    static uint32_t generationOf(ViewId id) { return static_cast<uint32_t>(id >> 32); }
    static ViewId makeId(uint32_t index, uint32_t generation) {
        return (static_cast<ViewId>(generation) << 32) | static_cast<ViewId>(index);
    }

    std::recursive_mutex& mutex() { return mutex_; }

private:
    struct Slot {
        std::unique_ptr<View> view;
        uint32_t generation = 1;
    };

    View* resolveLocked(ViewId id) const;

    mutable std::recursive_mutex mutex_;
    std::vector<Slot> slots_;
    std::vector<uint32_t> freeList_;
};

} // namespace xgu
