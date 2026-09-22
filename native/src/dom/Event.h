#pragma once

#include "core/Atom.h"
#include "core/RefCounted.h"
#include "core/WrapperSlot.h"

#include <cstdint>
#include <string>

namespace xgu::dom {

class Node;

enum class EventPhase : uint8_t {
    None = 0,
    Capturing = 1,
    AtTarget = 2,
    Bubbling = 3,
};

// Which JavaScript prototype the event gets. A game UI does not need the whole
// DOM event class hierarchy, so one C++ type carries every payload and the
// bindings pick the wrapper from this.
enum class EventCategory : uint8_t {
    Plain,    // Event
    Mouse,    // MouseEvent
    Wheel,    // WheelEvent
    Keyboard, // KeyboardEvent
    Input,    // InputEvent
    Focus,    // FocusEvent
};

// Mouse buttons, matching the DOM numbering.
enum MouseButton : int32_t {
    kMouseButtonNone = -1,
    kMouseButtonLeft = 0,
    kMouseButtonMiddle = 1,
    kMouseButtonRight = 2,
};

// Pressed-button bitmask, matching MouseEvent.buttons.
enum MouseButtons : uint32_t {
    kMouseButtonsLeft = 1 << 0,
    kMouseButtonsRight = 1 << 1,
    kMouseButtonsMiddle = 1 << 2,
};

struct Modifiers {
    bool alt = false;
    bool ctrl = false;
    bool shift = false;
    bool meta = false;
};

// A dispatched event. Reference counted because JavaScript may keep it past the
// dispatch, and because the dispatcher holds it while listeners run.
class Event : public RefCounted {
public:
    Event(const Atom& type, EventCategory category, bool bubbles, bool cancelable);
    ~Event() override;

    const Atom& type() const { return type_; }
    EventCategory category() const { return category_; }
    bool bubbles() const { return bubbles_; }
    bool cancelable() const { return cancelable_; }

    Node* target() const { return target_; }
    Node* currentTarget() const { return currentTarget_; }
    Node* relatedTarget() const { return relatedTarget_; }
    void setRelatedTarget(Node* node) { relatedTarget_ = node; }

    EventPhase phase() const { return phase_; }
    // Seconds since the runtime started, as performance.now()/1000 would report.
    double timeStamp() const { return timeStamp_; }
    void setTimeStamp(double seconds) { timeStamp_ = seconds; }

    bool defaultPrevented() const { return defaultPrevented_; }
    void preventDefault();
    void stopPropagation() { propagationStopped_ = true; }
    void stopImmediatePropagation();
    bool propagationStopped() const { return propagationStopped_; }
    bool immediatePropagationStopped() const { return immediateStopped_; }

    // True once the event has been handed to dispatchEvent; re-dispatching an
    // event object is refused, as in the DOM.
    bool dispatched() const { return dispatched_; }

    WrapperSlot& wrapperSlot() { return wrapper_; }

    // --- payload -------------------------------------------------------------
    // Mouse and wheel: position in CSS pixels relative to the view's top-left.
    float clientX = 0.0f;
    float clientY = 0.0f;
    // Movement since the previous mouse event.
    float movementX = 0.0f;
    float movementY = 0.0f;
    // Wheel only, in CSS pixels (deltaMode is always 0/pixels).
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    int32_t button = kMouseButtonNone;
    uint32_t buttons = 0;
    // Click count: 1 for click, 2 for dblclick.
    int32_t detail = 0;

    // Keyboard: `key` is the produced value ("a", "Enter", "ArrowLeft"), `code`
    // the physical key ("KeyA", "Enter", "ArrowLeft").
    std::string key;
    std::string code;
    bool repeat = false;

    // Input events: the text being inserted, and what caused the change.
    std::string data;
    std::string inputType;

    Modifiers modifiers;

private:
    friend bool dispatchEvent(Node& target, Event& event);

    Atom type_;
    EventCategory category_;
    bool bubbles_ = false;
    bool cancelable_ = false;
    bool defaultPrevented_ = false;
    bool propagationStopped_ = false;
    bool immediateStopped_ = false;
    bool dispatched_ = false;
    EventPhase phase_ = EventPhase::None;
    double timeStamp_ = 0.0;
    Node* target_ = nullptr;        // alive for the dispatch; see dispatchEvent
    Node* currentTarget_ = nullptr; // valid only while a listener runs
    Node* relatedTarget_ = nullptr;
    WrapperSlot wrapper_;
};

// Interned names of the events the runtime fires, so listener lookup and
// dispatch compare pointers instead of strings.
namespace eventNames {
const Atom& click();
const Atom& dblclick();
const Atom& mousedown();
const Atom& mouseup();
const Atom& mousemove();
const Atom& mouseover();
const Atom& mouseout();
const Atom& mouseenter();
const Atom& mouseleave();
const Atom& wheel();
const Atom& keydown();
const Atom& keyup();
const Atom& beforeinput();
const Atom& input();
const Atom& change();
const Atom& scroll();
const Atom& focus();
const Atom& blur();
const Atom& focusin();
const Atom& focusout();
const Atom& domContentLoaded();
const Atom& load();
} // namespace eventNames

} // namespace xgu::dom
