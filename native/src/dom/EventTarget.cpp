#include "dom/EventTarget.h"

#include "dom/Event.h"
#include "dom/Node.h"

#include <algorithm>

namespace xgu::dom {
namespace {

// Drops the registrations that were removed while listeners were running.
void compact(EventTargetData& data) {
    data.listeners.erase(std::remove_if(data.listeners.begin(), data.listeners.end(),
                                        [](const RegisteredListener& entry) { return entry.removed; }),
                         data.listeners.end());
}

} // namespace

bool Node::addEventListener(const Atom& type, RefPtr<EventListener> listener, bool capture, bool once) {
    if (!listener) {
        return false;
    }
    if (!events_) {
        events_ = std::make_unique<EventTargetData>();
    }
    for (const RegisteredListener& entry : events_->listeners) {
        if (!entry.removed && entry.type == type && entry.capture == capture && entry.listener &&
            entry.listener->isSameAs(*listener)) {
            return false; // the DOM ignores an identical re-registration
        }
    }
    events_->listeners.push_back(RegisteredListener{type, std::move(listener), capture, once, false});
    return true;
}

bool Node::removeEventListener(const Atom& type, const EventListener& listener, bool capture) {
    if (!events_) {
        return false;
    }
    for (RegisteredListener& entry : events_->listeners) {
        if (!entry.removed && entry.type == type && entry.capture == capture && entry.listener &&
            entry.listener->isSameAs(listener)) {
            entry.removed = true;
            if (events_->running == 0) {
                compact(*events_);
            }
            return true;
        }
    }
    return false;
}

bool Node::hasEventListener(const Atom& type) const {
    if (!events_) {
        return false;
    }
    for (const RegisteredListener& entry : events_->listeners) {
        if (!entry.removed && entry.type == type) {
            return true;
        }
    }
    return false;
}

void Node::runEventListeners(Event& event, bool capture) {
    if (!events_ || events_->listeners.empty()) {
        return;
    }

    // Snapshot: a listener may add or remove registrations, and the DOM says
    // the set is fixed when the phase starts.
    std::vector<RefPtr<EventListener>> toRun;
    std::vector<size_t> indices;
    for (size_t i = 0; i < events_->listeners.size(); ++i) {
        const RegisteredListener& entry = events_->listeners[i];
        if (!entry.removed && entry.capture == capture && entry.type == event.type() && entry.listener) {
            toRun.push_back(entry.listener);
            indices.push_back(i);
        }
    }
    if (toRun.empty()) {
        return;
    }

    // Keep the node alive: a listener may detach it and drop the last reference.
    RefPtr<Node> self(this);
    ++events_->running;
    for (size_t i = 0; i < toRun.size(); ++i) {
        if (event.immediatePropagationStopped()) {
            break;
        }
        // The registration may have been removed by an earlier listener in this
        // same phase, in which case it must not run.
        const size_t index = indices[i];
        if (index < events_->listeners.size()) {
            RegisteredListener& entry = events_->listeners[index];
            if (entry.removed || entry.listener != toRun[i]) {
                continue;
            }
            if (entry.once) {
                entry.removed = true;
            }
        }
        toRun[i]->handleEvent(event);
    }
    --events_->running;
    if (events_->running == 0) {
        compact(*events_);
    }
}

bool dispatchEvent(Node& target, Event& event) {
    if (event.dispatched()) {
        return !event.defaultPrevented(); // re-dispatch is not supported
    }
    event.dispatched_ = true;
    event.target_ = &target;

    // Hold the whole path: a listener may detach nodes from the tree, and the
    // remaining phases still have to reach the targets the path was built from.
    std::vector<RefPtr<Node>> path;
    for (Node* node = target.parentNode(); node; node = node->parentNode()) {
        path.emplace_back(node);
    }

    // Capture: outermost ancestor first.
    event.phase_ = EventPhase::Capturing;
    for (auto it = path.rbegin(); it != path.rend() && !event.propagationStopped(); ++it) {
        event.currentTarget_ = it->get();
        (*it)->runEventListeners(event, true);
    }

    if (!event.propagationStopped()) {
        event.phase_ = EventPhase::AtTarget;
        event.currentTarget_ = &target;
        // Both capture and bubble listeners run on the target itself.
        target.runEventListeners(event, true);
        if (!event.immediatePropagationStopped()) {
            target.runEventListeners(event, false);
        }
    }

    if (event.bubbles()) {
        event.phase_ = EventPhase::Bubbling;
        for (auto it = path.begin(); it != path.end() && !event.propagationStopped(); ++it) {
            event.currentTarget_ = it->get();
            (*it)->runEventListeners(event, false);
        }
    }

    event.phase_ = EventPhase::None;
    event.currentTarget_ = nullptr;
    return !event.defaultPrevented();
}

} // namespace xgu::dom
