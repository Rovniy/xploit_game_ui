#pragma once

namespace xgu::dom {

class Element;
class Node;

// Lets the DOM move focus without knowing about the input layer: element.focus()
// and element.blur() go through here, and the input router implements it.
class FocusController {
public:
    virtual ~FocusController() = default;

    // Focuses `element`, or clears the focus when it is null. Returns true when
    // the focus actually moved.
    virtual bool requestFocus(Element* element) = 0;
    virtual Element* focusedElement() const = 0;

    // Called before removal while nodes and their ancestor chains are alive.
    // Implementations must not dispatch script or mutate the tree here.
    virtual void subtreeWillDetach(Node&) {}
};

} // namespace xgu::dom
