#include "input/InputRouter.h"

#include "dom/Document.h"
#include "dom/Element.h"
#include "dom/EventTarget.h"
#include "dom/TextControl.h"
#include "html/HtmlTags.h"
#include "text/InlineContent.h"
#include "input/HitTester.h"
#include "layout/LayoutEngine.h"

#include <algorithm>
#include <cmath>

namespace xgu::input {
namespace {

using dom::Element;

// Two clicks within this window at nearly the same place make a dblclick.
constexpr double kDoubleClickSeconds = 0.5;
constexpr float kDoubleClickSlopPx = 4.0f;

const Atom& disabledAttribute() {
    static const Atom atom("disabled");
    return atom;
}

const Atom& hrefAttribute() {
    static const Atom atom("href");
    return atom;
}

const Atom& tabIndexAttribute() {
    static const Atom atom("tabindex");
    return atom;
}

bool isDisabled(const Element& element) { return element.hasAttribute(disabledAttribute()); }

bool isFocusable(const Element& element) {
    if (isDisabled(element)) {
        return false;
    }
    switch (element.knownTag()) {
    case html::HtmlTag::Input:
    case html::HtmlTag::Textarea:
    case html::HtmlTag::Button:
        return true;
    case html::HtmlTag::A:
        return element.hasAttribute(hrefAttribute());
    default:
        return element.hasAttribute(tabIndexAttribute());
    }
}

// The element and its ancestors, deepest first.
std::vector<Element*> chainOf(Element* element) {
    std::vector<Element*> chain;
    for (Element* current = element; current; current = current->parentElement()) {
        chain.push_back(current);
    }
    return chain;
}

bool chainContains(const std::vector<Element*>& chain, const Element* element) {
    return std::find(chain.begin(), chain.end(), element) != chain.end();
}

// Deepest element that is an ancestor-or-self of both, or null.
Element* commonAncestor(Element* a, Element* b) {
    if (!a || !b) {
        return nullptr;
    }
    const std::vector<Element*> chain = chainOf(a);
    for (Element* current = b; current; current = current->parentElement()) {
        if (chainContains(chain, current)) {
            return current;
        }
    }
    return nullptr;
}

} // namespace

InputRouter::InputRouter(dom::Document& document, layout::LayoutEngine& layout)
    : document_(document), layout_(layout) {}

InputRouter::~InputRouter() = default;

void InputRouter::reset() {
    for (Element* element : chainOf(hovered_)) {
        element->setState(Element::kStateHover, false);
    }
    for (Element* element : activeChain_) {
        element->setState(Element::kStateActive, false);
    }
    for (Element* element : chainOf(focused_)) {
        element->setState(Element::kStateFocus | Element::kStateFocusWithin, false);
    }
    hovered_ = nullptr;
    focused_ = nullptr;
    pressTarget_ = nullptr;
    draggingIn_ = nullptr;
    activeChain_.clear();
    clickCount_ = 0;
    lastClickTime_ = -1.0;
    hasLastPosition_ = false;
}

bool InputRouter::isHovered(const Element& element) const { return element.hasState(Element::kStateHover); }
bool InputRouter::isActive(const Element& element) const { return element.hasState(Element::kStateActive); }
bool InputRouter::isFocused(const Element& element) const { return element.hasState(Element::kStateFocus); }
bool InputRouter::isFocusWithin(const Element& element) const {
    return element.hasState(Element::kStateFocus | Element::kStateFocusWithin);
}

Element* InputRouter::hitTestAt(float x, float y) {
    layout::LayoutBox* root = layout_.root();
    if (!root) {
        return nullptr;
    }
    return hitTest(*root, x, y).element;
}

bool InputRouter::fire(Element& target, const Atom& type, dom::EventCategory category, bool bubbles, bool cancelable,
                       const InputEvent& source, Element* relatedTarget, int32_t detail) {
    RefPtr<dom::Event> event = makeRef<dom::Event>(type, category, bubbles, cancelable);
    event->setTimeStamp(source.time);
    event->modifiers = source.modifiers;
    event->clientX = source.x;
    event->clientY = source.y;
    event->button = source.button;
    event->buttons = source.buttons;
    event->detail = detail;
    event->deltaX = source.deltaX;
    event->deltaY = source.deltaY;
    event->key = source.key;
    event->code = source.code;
    event->repeat = source.repeat;
    event->data = source.text;
    event->setRelatedTarget(relatedTarget);
    if (hasLastPosition_) {
        event->movementX = source.x - lastX_;
        event->movementY = source.y - lastY_;
    }
    return dom::dispatchEvent(target, *event);
}

Element* InputRouter::focusableFor(Element* element) {
    for (Element* current = element; current; current = current->parentElement()) {
        if (isFocusable(*current)) {
            return current;
        }
    }
    return nullptr;
}

bool InputRouter::setFocus(Element* element) {
    if (element == focused_) {
        return false;
    }
    Element* previous = focused_;
    // Keep both alive across the dispatches: a listener may detach either.
    RefPtr<Element> keepPrevious(previous);
    RefPtr<Element> keepNext(element);

    for (Element* node : chainOf(previous)) {
        node->setState(node == previous ? (Element::kStateFocus | Element::kStateFocusWithin)
                                        : Element::kStateFocusWithin,
                       false);
    }
    focused_ = element;
    for (Element* node : chainOf(element)) {
        node->setState(node == element ? (Element::kStateFocus | Element::kStateFocusWithin)
                                       : Element::kStateFocusWithin,
                       true);
    }

    InputEvent synthetic;
    if (previous) {
        if (dom::TextControl* control = previous->textControl(); control && control->changedSinceCommit()) {
            control->markCommitted();
            fire(*previous, dom::eventNames::change(), dom::EventCategory::Plain, true, false, synthetic);
        }
        fire(*previous, dom::eventNames::blur(), dom::EventCategory::Focus, false, false, synthetic, element);
        fire(*previous, dom::eventNames::focusout(), dom::EventCategory::Focus, true, false, synthetic, element);
    }
    if (element) {
        fire(*element, dom::eventNames::focus(), dom::EventCategory::Focus, false, false, synthetic, previous);
        fire(*element, dom::eventNames::focusin(), dom::EventCategory::Focus, true, false, synthetic, previous);
    }
    return true;
}

bool InputRouter::updateHover(Element* target, const InputEvent& event) {
    if (target == hovered_) {
        return false;
    }
    Element* previous = hovered_;
    RefPtr<Element> keepPrevious(previous);
    RefPtr<Element> keepTarget(target);

    const std::vector<Element*> oldChain = chainOf(previous);
    const std::vector<Element*> newChain = chainOf(target);
    hovered_ = target;

    for (Element* element : oldChain) {
        if (!chainContains(newChain, element)) {
            element->setState(Element::kStateHover, false);
        }
    }
    for (Element* element : newChain) {
        element->setState(Element::kStateHover, true);
    }

    // mouseout/mouseover bubble and carry the other side as relatedTarget;
    // mouseleave/mouseenter do not bubble and fire once per element left/entered.
    if (previous) {
        fire(*previous, dom::eventNames::mouseout(), dom::EventCategory::Mouse, true, false, event, target);
    }
    for (Element* element : oldChain) {
        if (!chainContains(newChain, element)) {
            fire(*element, dom::eventNames::mouseleave(), dom::EventCategory::Mouse, false, false, event, target);
        }
    }
    if (target) {
        fire(*target, dom::eventNames::mouseover(), dom::EventCategory::Mouse, true, false, event, previous);
    }
    // Outermost first, as the DOM specifies for mouseenter.
    for (auto it = newChain.rbegin(); it != newChain.rend(); ++it) {
        if (!chainContains(oldChain, *it)) {
            fire(**it, dom::eventNames::mouseenter(), dom::EventCategory::Mouse, false, false, event, previous);
        }
    }
    return true;
}

bool InputRouter::setActiveChain(Element* target) {
    bool changed = false;
    const std::vector<Element*> chain = chainOf(target);
    for (Element* element : activeChain_) {
        if (!chainContains(chain, element)) {
            changed |= element->setState(Element::kStateActive, false);
        }
    }
    for (Element* element : chain) {
        changed |= element->setState(Element::kStateActive, true);
    }
    activeChain_ = chain;
    return changed;
}

bool InputRouter::handleMouseMove(const InputEvent& event) {
    Element* target = hitTestAt(event.x, event.y);
    bool changed = updateHover(target, event);
    if (draggingIn_) {
        placeCaretAt(*draggingIn_, event.x, event.y, true);
        draggingIn_->markDirty(dom::kDirtyPaintSelf);
        changed = true;
    }
    if (target) {
        fire(*target, dom::eventNames::mousemove(), dom::EventCategory::Mouse, true, true, event);
        changed = true;
    }
    lastX_ = event.x;
    lastY_ = event.y;
    hasLastPosition_ = true;
    return changed;
}

namespace {

// The inline box holding a control's text, or null.
text::InlineContent* contentOf(layout::LayoutBox* box) {
    if (!box) {
        return nullptr;
    }
    if (text::InlineContent* own = box->inlineContent()) {
        return own;
    }
    for (const std::unique_ptr<layout::LayoutBox>& child : box->children()) {
        if (text::InlineContent* content = child->inlineContent()) {
            return content;
        }
    }
    return nullptr;
}

} // namespace

void InputRouter::placeCaretAt(Element& element, float x, float y, bool extend) {
    dom::TextControl* control = element.textControl();
    layout::LayoutBox* box = layout_.boxFor(element);
    text::InlineContent* content = contentOf(box);
    if (!control || !content || control->showingPlaceholder()) {
        if (control) {
            control->collapseTo(0);
        }
        return;
    }
    // The paragraph sits at the content box of the inline child.
    layout::LayoutBox* inlineBox = box->inlineContent() ? box : box->children().front().get();
    const layout::Rect frame = inlineBox->contentBox();
    const size_t offset = content->offsetAtPoint(x - frame.x, y - frame.y);
    if (extend) {
        control->setSelection(control->anchor(), offset);
    } else {
        control->collapseTo(offset);
    }
}

void InputRouter::afterEdit(Element& element, const InputEvent& event, const char* inputType) {
    // The value drives layout, so the box tree has to be rebuilt from it.
    element.markDirty(dom::kDirtyLayoutTree | dom::kDirtyLayout | dom::kDirtyPaintSelf);
    InputEvent source = event;
    source.text = inputType == std::string("insertText") ? event.text : std::string();
    RefPtr<dom::Event> fired = makeRef<dom::Event>(dom::eventNames::input(), dom::EventCategory::Input, true, false);
    fired->setTimeStamp(event.time);
    fired->data = source.text;
    fired->inputType = inputType;
    dom::dispatchEvent(element, *fired);
}

bool InputRouter::editWithKey(Element& element, const InputEvent& event) {
    dom::TextControl* control = element.textControl();
    if (!control || !control->isEditable()) {
        return false;
    }
    const bool shift = event.modifiers.shift;
    const bool ctrl = event.modifiers.ctrl;

    if (ctrl && (event.key == "a" || event.key == "A")) {
        control->selectAll();
        element.markDirty(dom::kDirtyPaintSelf);
        return true;
    }
    if (event.key == "ArrowLeft") {
        control->moveLeft(shift);
        element.markDirty(dom::kDirtyPaintSelf);
        return true;
    }
    if (event.key == "ArrowRight") {
        control->moveRight(shift);
        element.markDirty(dom::kDirtyPaintSelf);
        return true;
    }
    if (event.key == "Home") {
        control->moveToStart(shift);
        element.markDirty(dom::kDirtyPaintSelf);
        return true;
    }
    if (event.key == "End") {
        control->moveToEnd(shift);
        element.markDirty(dom::kDirtyPaintSelf);
        return true;
    }
    if (event.key == "Backspace" || event.key == "Delete") {
        const bool backward = event.key == "Backspace";
        InputEvent probe = event;
        probe.text.clear();
        if (!fire(element, dom::eventNames::beforeinput(), dom::EventCategory::Input, true, true, probe)) {
            return true;
        }
        if (backward ? control->deleteBackward() : control->deleteForward()) {
            afterEdit(element, probe, backward ? "deleteContentBackward" : "deleteContentForward");
        }
        return true;
    }
    if (event.key == "Enter") {
        if (control->isMultiline()) {
            InputEvent insertion = event;
            insertion.text = "\n";
            if (!fire(element, dom::eventNames::beforeinput(), dom::EventCategory::Input, true, true, insertion)) {
                return true;
            }
            control->insertText(u"\n");
            afterEdit(element, insertion, "insertText");
            return true;
        }
        // A single-line field commits on Enter, as browsers do.
        if (control->changedSinceCommit()) {
            control->markCommitted();
            fire(element, dom::eventNames::change(), dom::EventCategory::Plain, true, false, event);
        }
        return false; // the page still sees keydown
    }
    return false;
}

bool InputRouter::handleMouseDown(const InputEvent& event) {
    Element* target = hitTestAt(event.x, event.y);
    updateHover(target, event);
    pressTarget_ = target;
    setActiveChain(target);

    bool defaultAllowed = true;
    if (target) {
        defaultAllowed = fire(*target, dom::eventNames::mousedown(), dom::EventCategory::Mouse, true, true, event);
    }
    if (defaultAllowed && event.button == dom::kMouseButtonLeft) {
        // Focus follows the press, and lands on the nearest focusable ancestor.
        Element* focusTarget = focusableFor(target);
        setFocus(focusTarget);
        if (focusTarget && focusTarget->textControl() && focusTarget->textControl()->isEditable()) {
            placeCaretAt(*focusTarget, event.x, event.y, event.modifiers.shift);
            focusTarget->markDirty(dom::kDirtyPaintSelf);
            draggingIn_ = focusTarget;
        }
    }
    return true;
}

bool InputRouter::handleMouseUp(const InputEvent& event) {
    Element* target = hitTestAt(event.x, event.y);
    updateHover(target, event);
    setActiveChain(nullptr);
    draggingIn_ = nullptr;

    if (target) {
        fire(*target, dom::eventNames::mouseup(), dom::EventCategory::Mouse, true, true, event);
    }

    // A click fires on the deepest element that saw both the press and release.
    Element* clickTarget = commonAncestor(pressTarget_, target);
    pressTarget_ = nullptr;
    if (!clickTarget || event.button != dom::kMouseButtonLeft) {
        return true;
    }

    const bool nearLast = std::fabs(event.x - lastClickX_) <= kDoubleClickSlopPx &&
                          std::fabs(event.y - lastClickY_) <= kDoubleClickSlopPx;
    if (lastClickTime_ >= 0.0 && event.time - lastClickTime_ <= kDoubleClickSeconds && nearLast) {
        ++clickCount_;
    } else {
        clickCount_ = 1;
    }
    lastClickTime_ = event.time;
    lastClickX_ = event.x;
    lastClickY_ = event.y;

    RefPtr<Element> keepTarget(clickTarget);
    fire(*clickTarget, dom::eventNames::click(), dom::EventCategory::Mouse, true, true, event, nullptr, clickCount_);
    if (clickCount_ == 2) {
        fire(*clickTarget, dom::eventNames::dblclick(), dom::EventCategory::Mouse, true, true, event, nullptr, 2);
    }
    return true;
}

bool InputRouter::handleWheel(const InputEvent& event) {
    Element* target = hitTestAt(event.x, event.y);
    if (!target) {
        return false;
    }
    RefPtr<Element> keepTarget(target);
    if (!fire(*target, dom::eventNames::wheel(), dom::EventCategory::Wheel, true, true, event)) {
        return true; // the page handled the wheel itself
    }
    // Default action: scroll the nearest ancestor that can still move in this
    // direction, the way a browser chains scrolling outwards.
    return scrollNearest(*target, event.deltaX, event.deltaY);
}

bool InputRouter::scrollNearest(Element& from, float deltaX, float deltaY) {
    for (Element* element = &from; element; element = element->parentElement()) {
        layout::LayoutBox* box = layout_.boxFor(*element);
        if (!box) {
            continue;
        }
        const bool canX = deltaX != 0.0f && box->scrollsHorizontally() &&
                          ((deltaX > 0.0f && box->scrollLeft() < box->maxScrollLeft()) ||
                           (deltaX < 0.0f && box->scrollLeft() > 0.0f));
        const bool canY = deltaY != 0.0f && box->scrollsVertically() &&
                          ((deltaY > 0.0f && box->scrollTop() < box->maxScrollTop()) ||
                           (deltaY < 0.0f && box->scrollTop() > 0.0f));
        if (!canX && !canY) {
            continue;
        }
        if (!box->setScroll(box->scrollLeft() + (canX ? deltaX : 0.0f),
                            box->scrollTop() + (canY ? deltaY : 0.0f))) {
            continue;
        }
        element->markDirty(dom::kDirtyPaintSelf | dom::kDirtyPaintChildren);
        InputEvent synthetic;
        // `scroll` does not bubble in the DOM; listeners go on the box itself.
        fire(*element, dom::eventNames::scroll(), dom::EventCategory::Plain, false, false, synthetic);
        return true;
    }
    return false;
}

bool InputRouter::handleKey(const InputEvent& event) {
    // Keys go to the focused element, or to the body when nothing has focus.
    Element* target = focused_ ? focused_ : document_.body();
    if (!target) {
        target = document_.documentElement();
    }
    if (!target) {
        return false;
    }
    const Atom& type = event.type == InputEventType::KeyDown ? dom::eventNames::keydown() : dom::eventNames::keyup();
    const bool defaultAllowed = fire(*target, type, dom::EventCategory::Keyboard, true, true, event);
    if (defaultAllowed && event.type == InputEventType::KeyDown && target->textControl()) {
        editWithKey(*target, event);
    }
    return true;
}

bool InputRouter::handleTextInput(const InputEvent& event) {
    Element* target = focused_;
    if (!target || event.text.empty()) {
        return false;
    }
    if (!fire(*target, dom::eventNames::beforeinput(), dom::EventCategory::Input, true, true, event)) {
        return true; // the page cancelled the insertion
    }
    dom::TextControl* control = target->textControl();
    if (control && control->isEditable()) {
        control->insertText(dom::utf8ToUtf16(event.text));
        afterEdit(*target, event, "insertText");
        return true;
    }
    fire(*target, dom::eventNames::input(), dom::EventCategory::Input, true, false, event);
    return true;
}

bool InputRouter::handle(const InputEvent& event) {
    if (!document_.documentElement()) {
        return false;
    }
    switch (event.type) {
    case InputEventType::MouseMove:
        return handleMouseMove(event);
    case InputEventType::MouseDown:
        return handleMouseDown(event);
    case InputEventType::MouseUp:
        return handleMouseUp(event);
    case InputEventType::Wheel:
        return handleWheel(event);
    case InputEventType::PointerLeave:
        return updateHover(nullptr, event);
    case InputEventType::KeyDown:
    case InputEventType::KeyUp:
        return handleKey(event);
    case InputEventType::TextInput:
        return handleTextInput(event);
    case InputEventType::TouchBegin:
    case InputEventType::TouchMove:
    case InputEventType::TouchEnd: {
        // The first finger is mirrored to the mouse, which is what game UI
        // pages expect; multi-touch is not exposed.
        if (event.touchId != 0) {
            return false;
        }
        InputEvent mouse = event;
        mouse.button = dom::kMouseButtonLeft;
        mouse.buttons = event.type == InputEventType::TouchEnd ? 0u : dom::kMouseButtonsLeft;
        mouse.type = event.type == InputEventType::TouchBegin  ? InputEventType::MouseDown
                     : event.type == InputEventType::TouchMove ? InputEventType::MouseMove
                                                               : InputEventType::MouseUp;
        const bool changed = handle(mouse);
        if (mouse.type == InputEventType::MouseUp) {
            return updateHover(nullptr, event) || changed;
        }
        return changed;
    }
    case InputEventType::WindowBlur: {
        bool changed = updateHover(nullptr, event);
        changed |= setActiveChain(nullptr);
        changed |= setFocus(nullptr);
        pressTarget_ = nullptr;
        return changed;
    }
    }
    return false;
}

} // namespace xgu::input
