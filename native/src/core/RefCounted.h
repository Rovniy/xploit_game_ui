#pragma once

#include <cstdint>
#include <utility>

namespace xgu {

// Intrusive reference counting. Objects start with a count of one, so the
// creator owns the first reference (adopt it into a RefPtr with adoptRef).
//
// Not thread-safe by design: DOM nodes live on the runtime thread only.
class RefCounted {
public:
    RefCounted(const RefCounted&) = delete;
    RefCounted& operator=(const RefCounted&) = delete;

    void ref() const { ++refCount_; }
    void deref() const {
        if (--refCount_ == 0) {
            delete this;
        }
    }
    uint32_t refCount() const { return refCount_; }

protected:
    RefCounted() = default;
    virtual ~RefCounted() = default;

private:
    mutable uint32_t refCount_ = 1;
};

template <typename T>
class RefPtr {
public:
    RefPtr() = default;
    RefPtr(std::nullptr_t) {}

    // Takes a new reference.
    RefPtr(T* pointer) : pointer_(pointer) {
        if (pointer_) {
            pointer_->ref();
        }
    }
    RefPtr(const RefPtr& other) : RefPtr(other.pointer_) {}
    template <typename U>
    RefPtr(const RefPtr<U>& other) : RefPtr(other.get()) {}

    RefPtr(RefPtr&& other) noexcept : pointer_(other.pointer_) { other.pointer_ = nullptr; }
    template <typename U>
    RefPtr(RefPtr<U>&& other) noexcept : pointer_(other.leakRef()) {}

    ~RefPtr() {
        if (pointer_) {
            pointer_->deref();
        }
    }

    RefPtr& operator=(const RefPtr& other) {
        RefPtr copy(other);
        swap(copy);
        return *this;
    }
    RefPtr& operator=(RefPtr&& other) noexcept {
        RefPtr moved(std::move(other));
        swap(moved);
        return *this;
    }
    RefPtr& operator=(T* pointer) {
        RefPtr copy(pointer);
        swap(copy);
        return *this;
    }

    void swap(RefPtr& other) noexcept { std::swap(pointer_, other.pointer_); }
    void reset() { RefPtr().swap(*this); }

    T* get() const { return pointer_; }
    T* operator->() const { return pointer_; }
    T& operator*() const { return *pointer_; }
    explicit operator bool() const { return pointer_ != nullptr; }

    // Hands the reference to the caller without releasing it.
    T* leakRef() {
        T* result = pointer_;
        pointer_ = nullptr;
        return result;
    }

    friend bool operator==(const RefPtr& a, const RefPtr& b) { return a.pointer_ == b.pointer_; }
    friend bool operator==(const RefPtr& a, const T* b) { return a.pointer_ == b; }

private:
    T* pointer_ = nullptr;
};

// Adopts an already-owned reference (the one every object starts with).
template <typename T>
RefPtr<T> adoptRef(T* pointer) {
    RefPtr<T> result(pointer); // takes a second reference
    if (pointer) {
        pointer->deref(); // ... and gives back the initial one
    }
    return result;
}

// Creates an object and adopts its initial reference.
template <typename T, typename... Args>
RefPtr<T> makeRef(Args&&... args) {
    return adoptRef(new T(std::forward<Args>(args)...));
}

} // namespace xgu
