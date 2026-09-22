#include "core/ViewRegistry.h"

namespace xgu {

ViewId ViewRegistry::add(std::unique_ptr<View> view) {
    std::lock_guard lock(mutex_);
    uint32_t index;
    if (!freeList_.empty()) {
        index = freeList_.back();
        freeList_.pop_back();
    } else {
        index = static_cast<uint32_t>(slots_.size());
        slots_.emplace_back();
    }
    Slot& slot = slots_[index];
    slot.view = std::move(view);
    const ViewId id = makeId(index, slot.generation);
    slot.view->setId(id); // so the view can attribute its log messages
    return id;
}

View* ViewRegistry::resolveLocked(ViewId id) const {
    if (id == kInvalid) {
        return nullptr;
    }
    const uint32_t index = indexOf(id);
    if (index >= slots_.size()) {
        return nullptr;
    }
    const Slot& slot = slots_[index];
    if (!slot.view || slot.generation != generationOf(id)) {
        return nullptr;
    }
    return slot.view.get();
}

View* ViewRegistry::resolve(ViewId id) const {
    std::lock_guard lock(mutex_);
    return resolveLocked(id);
}

bool ViewRegistry::withView(ViewId id, const std::function<void(View&)>& fn) {
    std::lock_guard lock(mutex_);
    View* view = resolveLocked(id);
    if (!view) {
        return false;
    }
    fn(*view);
    return true;
}

void ViewRegistry::forEach(const std::function<void(ViewId, View&)>& fn) {
    std::lock_guard lock(mutex_);
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].view) {
            fn(makeId(i, slots_[i].generation), *slots_[i].view);
        }
    }
}

std::unique_ptr<View> ViewRegistry::remove(ViewId id) {
    std::lock_guard lock(mutex_);
    if (!resolveLocked(id)) {
        return nullptr;
    }
    Slot& slot = slots_[indexOf(id)];
    std::unique_ptr<View> view = std::move(slot.view);
    slot.generation++;
    if (slot.generation == 0) {
        slot.generation = 1; // never hand out generation 0 (id 0 is invalid)
    }
    freeList_.push_back(indexOf(id));
    return view;
}

std::vector<std::unique_ptr<View>> ViewRegistry::removeAll() {
    std::lock_guard lock(mutex_);
    std::vector<std::unique_ptr<View>> result;
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        if (slot.view) {
            result.push_back(std::move(slot.view));
            slot.generation++;
            if (slot.generation == 0) {
                slot.generation = 1;
            }
            freeList_.push_back(i);
        }
    }
    return result;
}

size_t ViewRegistry::size() const {
    std::lock_guard lock(mutex_);
    return slots_.size() - freeList_.size();
}

} // namespace xgu
