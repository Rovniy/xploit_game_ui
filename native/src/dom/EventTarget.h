#pragma once

#include "core/Atom.h"
#include "core/RefCounted.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace xgu::dom {

class Event;
class Node;

// A registered handler. The DOM knows nothing about JavaScript: the bindings
// subclass this and hold a v8::Global<Function>, and native code (the UA
// behaviours, tests) subclasses it with a plain callback.
class EventListener : public RefCounted {
public:
    virtual void handleEvent(Event& event) = 0;

    // Identity for removeEventListener: two registrations are the same when
    // they wrap the same underlying handler. Implementations compare typeTag()
    // first, so no RTTI is needed.
    virtual bool isSameAs(const EventListener& other) const = 0;

    // Address of a per-subclass static object, used as a type discriminator.
    virtual const void* typeTag() const = 0;
};

// A listener backed by a plain callback, for native behaviour and tests.
class FunctionEventListener final : public EventListener {
public:
    using Callback = std::function<void(Event&)>;

    explicit FunctionEventListener(Callback callback) : callback_(std::move(callback)) {}

    void handleEvent(Event& event) override {
        if (callback_) {
            callback_(event);
        }
    }
    bool isSameAs(const EventListener& other) const override { return &other == this; }
    const void* typeTag() const override { return tag(); }

    static const void* tag() {
        static const char kTag = 0;
        return &kTag;
    }

private:
    Callback callback_;
};

// One node's registrations. Kept in insertion order, which is the order the DOM
// requires listeners to run in.
struct RegisteredListener {
    Atom type;
    RefPtr<EventListener> listener;
    bool capture = false;
    bool once = false;
    // Set instead of erasing when a listener is removed while it is running.
    bool removed = false;
};

struct EventTargetData {
    std::vector<RegisteredListener> listeners;
    // Non-zero while listeners of this node are running, so removals defer to
    // compaction instead of invalidating the iteration.
    uint32_t running = 0;
};

// Runs `event` over the path from the document down to `target` (capture), on
// the target, and back up (bubble, when the event bubbles). Returns false when
// a listener called preventDefault, which is what dispatchEvent reports and
// what tells the caller to skip the default action.
bool dispatchEvent(Node& target, Event& event);

} // namespace xgu::dom
