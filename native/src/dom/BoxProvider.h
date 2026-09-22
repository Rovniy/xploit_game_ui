#pragma once

namespace xgu::layout {
class LayoutBox;
}

namespace xgu::dom {

class Element;

// Gives the DOM the laid-out box of an element without knowing about the layout
// engine. getBoundingClientRect and the scroll properties need the geometry, and
// the layout engine owns the box tree, so it implements this.
class BoxProvider {
public:
    virtual ~BoxProvider() = default;

    // The element's box, or null before the first layout or when the element
    // generates none (display: none).
    virtual layout::LayoutBox* boxFor(const Element& element) const = 0;
};

} // namespace xgu::dom
