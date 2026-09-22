#pragma once

#include "css/SelectorMatcher.h"
#include "dom/Event.h"
#include "dom/FocusController.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xgu::dom {
class Document;
class Element;
} // namespace xgu::dom

namespace xgu::layout {
class LayoutEngine;
}

namespace xgu::input {

// What the host sends in. One flat struct crosses the C ABI, so C# fills the
// fields that matter for the type it is sending.
enum class InputEventType : uint8_t {
    MouseMove,
    MouseDown,
    MouseUp,
    Wheel,
    PointerLeave, // the pointer left the view
    KeyDown,
    KeyUp,
    TextInput,
    TouchBegin,
    TouchMove,
    TouchEnd,
    WindowBlur, // the host window lost focus
};

struct InputEvent {
    InputEventType type = InputEventType::MouseMove;
    // Position in CSS pixels from the view's top-left.
    float x = 0.0f;
    float y = 0.0f;
    // Wheel movement in CSS pixels.
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    int32_t button = dom::kMouseButtonNone;
    uint32_t buttons = 0;
    dom::Modifiers modifiers;
    // KeyDown/KeyUp: the produced value and the physical key.
    std::string key;
    std::string code;
    // TextInput: the composed text the host produced.
    std::string text;
    int32_t touchId = 0;
    // Seconds since the runtime started.
    double time = 0.0;
    bool repeat = false;
};

// Turns host input into DOM events and keeps :hover/:active/:focus in step.
//
// Lives on the runtime thread with the document it serves. It is the document's
// ElementStateProvider, so a state change here is picked up by the next style
// recalculation.
class InputRouter final : public css::ElementStateProvider, public dom::FocusController {
public:
    InputRouter(dom::Document& document, layout::LayoutEngine& layout);
    ~InputRouter() override;

    // Returns true when the document changed and needs restyle, layout or paint.
    bool handle(const InputEvent& event);

    // Drops hover, active and focus. Called when the document is replaced.
    void reset();

    // dom::FocusController: moves focus, firing blur/focusout and focus/focusin.
    // A null element just clears the focus.
    bool requestFocus(dom::Element* element) override { return setFocus(element); }
    dom::Element* focusedElement() const override { return focused_; }
    bool setFocus(dom::Element* element);

    dom::Element* hoveredElement() const { return hovered_; }

    // css::ElementStateProvider
    bool isHovered(const dom::Element& element) const override;
    bool isActive(const dom::Element& element) const override;
    bool isFocused(const dom::Element& element) const override;
    bool isFocusWithin(const dom::Element& element) const override;

private:
    dom::Element* hitTestAt(float x, float y);
    // Fires one event at `target` and reports whether the default action stands.
    bool fire(dom::Element& target, const Atom& type, dom::EventCategory category, bool bubbles, bool cancelable,
              const InputEvent& source, dom::Element* relatedTarget = nullptr, int32_t detail = 0);

    bool handleMouseMove(const InputEvent& event);
    bool handleMouseDown(const InputEvent& event);
    bool handleMouseUp(const InputEvent& event);
    bool handleWheel(const InputEvent& event);
    bool handleKey(const InputEvent& event);
    bool handleTextInput(const InputEvent& event);

    // Text control editing. Returns true when the key was consumed.
    bool editWithKey(dom::Element& element, const InputEvent& event);
    // Puts the caret where the pointer is, and starts a drag selection.
    void placeCaretAt(dom::Element& element, float x, float y, bool extend);
    // Tells layout the value changed and fires beforeinput/input.
    void afterEdit(dom::Element& element, const InputEvent& event, const char* inputType);

    // Replaces the hover chain, firing mouseover/out and mouseenter/leave.
    bool updateHover(dom::Element* target, const InputEvent& event);
    bool setActiveChain(dom::Element* target);
    // Nearest ancestor-or-self that can take focus, or null.
    static dom::Element* focusableFor(dom::Element* element);

    dom::Document& document_;
    layout::LayoutEngine& layout_;

    // Deepest hovered element; its ancestors carry kStateHover too.
    dom::Element* hovered_ = nullptr;
    dom::Element* focused_ = nullptr;
    // Elements currently carrying :active, deepest first.
    std::vector<dom::Element*> activeChain_;
    // Where the last mousedown landed, for synthesising click.
    dom::Element* pressTarget_ = nullptr;
    double lastClickTime_ = -1.0;
    float lastClickX_ = 0.0f;
    float lastClickY_ = 0.0f;
    int32_t clickCount_ = 0;
    // Set between mousedown and mouseup inside a text control, so dragging
    // extends the selection.
    dom::Element* draggingIn_ = nullptr;
    float lastX_ = 0.0f;
    float lastY_ = 0.0f;
    bool hasLastPosition_ = false;
};

} // namespace xgu::input
