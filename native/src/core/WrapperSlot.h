#pragma once

namespace xgu {

// Opaque, self-destroying slot a scripting layer uses to cache the wrapper
// object it created for a native object. The native side knows nothing about
// V8: it only stores a pointer and the function that releases it.
//
// The wrapper owns a strong reference to the native object, while this slot
// holds the weak side of the cycle, so dropping the wrapper in script releases
// the reference.
struct WrapperSlot {
    void* data = nullptr;
    void (*destroy)(void*) = nullptr;

    WrapperSlot() = default;
    WrapperSlot(const WrapperSlot&) = delete;
    WrapperSlot& operator=(const WrapperSlot&) = delete;
    ~WrapperSlot() { clear(); }

    void clear() {
        // Reset first: the callback may release the last reference to the object
        // that owns this slot, and nothing may touch `this` afterwards.
        void* owned = data;
        void (*deleter)(void*) = destroy;
        data = nullptr;
        destroy = nullptr;
        if (owned && deleter) {
            deleter(owned);
        }
    }
};

} // namespace xgu
