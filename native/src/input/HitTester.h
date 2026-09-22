#pragma once

namespace xgu::dom {
class Element;
}

namespace xgu::layout {
class LayoutBox;
}

namespace xgu::input {

struct HitResult {
    layout::LayoutBox* box = nullptr;
    // The element the event targets. Anonymous boxes report their parent's
    // element, so text always hits the block that contains it.
    dom::Element* element = nullptr;
    // The point in the box's own coordinate space, after undoing the transforms
    // of the box and its ancestors.
    float localX = 0.0f;
    float localY = 0.0f;

    explicit operator bool() const { return element != nullptr; }
};

// Topmost box at (x, y), in CSS pixels relative to the view's top-left.
//
// The walk mirrors paint::Painter exactly, in reverse: within a stacking context
// it visits positive z-index contexts, then positioned descendants, then inline
// content, then in-flow children, then negative z-index contexts, so whatever
// was painted last is tested first. Transforms are inverted on the way down,
// `overflow: hidden` clips, and `pointer-events: none` and `visibility: hidden`
// subtrees are skipped.
HitResult hitTest(layout::LayoutBox& root, float x, float y);

} // namespace xgu::input
