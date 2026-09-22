#include "dom/Event.h"

namespace xgu::dom {

Event::Event(const Atom& type, EventCategory category, bool bubbles, bool cancelable)
    : type_(type), category_(category), bubbles_(bubbles), cancelable_(cancelable) {}

Event::~Event() = default;

void Event::preventDefault() {
    if (cancelable_) {
        defaultPrevented_ = true;
    }
}

void Event::stopImmediatePropagation() {
    propagationStopped_ = true;
    immediateStopped_ = true;
}

namespace eventNames {

#define XGU_EVENT_NAME(fn, text)                                                                                     \
    const Atom& fn() {                                                                                               \
        static const Atom atom(text);                                                                                \
        return atom;                                                                                                 \
    }

XGU_EVENT_NAME(click, "click")
XGU_EVENT_NAME(dblclick, "dblclick")
XGU_EVENT_NAME(mousedown, "mousedown")
XGU_EVENT_NAME(mouseup, "mouseup")
XGU_EVENT_NAME(mousemove, "mousemove")
XGU_EVENT_NAME(mouseover, "mouseover")
XGU_EVENT_NAME(mouseout, "mouseout")
XGU_EVENT_NAME(mouseenter, "mouseenter")
XGU_EVENT_NAME(mouseleave, "mouseleave")
XGU_EVENT_NAME(wheel, "wheel")
XGU_EVENT_NAME(keydown, "keydown")
XGU_EVENT_NAME(keyup, "keyup")
XGU_EVENT_NAME(beforeinput, "beforeinput")
XGU_EVENT_NAME(input, "input")
XGU_EVENT_NAME(change, "change")
XGU_EVENT_NAME(focus, "focus")
XGU_EVENT_NAME(blur, "blur")
XGU_EVENT_NAME(focusin, "focusin")
XGU_EVENT_NAME(focusout, "focusout")
XGU_EVENT_NAME(domContentLoaded, "DOMContentLoaded")
XGU_EVENT_NAME(load, "load")

#undef XGU_EVENT_NAME

} // namespace eventNames

} // namespace xgu::dom
