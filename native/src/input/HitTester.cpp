#include "input/HitTester.h"

#include "css/ComputedStyle.h"
#include "dom/Element.h"
#include "layout/LayoutBox.h"
#include "paint/BoxGeometry.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace xgu::input {
namespace {

using css::ComputedStyle;
using layout::LayoutBox;

bool acceptsPointerEvents(const LayoutBox& box) {
    const ComputedStyle* style = box.style();
    return style && style->pointerEvents != css::PointerEvents::None;
}

// The element an event on this box targets. Anonymous boxes (inline formatting
// contexts) belong to their parent element.
dom::Element* targetElement(LayoutBox& box) {
    for (LayoutBox* current = &box; current; current = current->parent()) {
        if (dom::Element* element = current->element()) {
            return element;
        }
    }
    return nullptr;
}

struct Tester {
    HitResult result;

    // Runs the box subtree with the point already in the box's parent space.
    bool testBox(LayoutBox& box, float px, float py);
    bool testChildren(LayoutBox& box, float px, float py);
    bool record(LayoutBox& box, float px, float py);
};

bool Tester::record(LayoutBox& box, float px, float py) {
    if (!acceptsPointerEvents(box) || !paint::isVisible(box)) {
        return false;
    }
    dom::Element* element = targetElement(box);
    if (!element) {
        return false;
    }
    result.box = &box;
    result.element = element;
    result.localX = px;
    result.localY = py;
    return true;
}

bool Tester::testChildren(LayoutBox& box, float px, float py) {
    // Same buckets as paint::Painter::paintChildren.
    std::vector<LayoutBox*> negative;
    std::vector<LayoutBox*> inFlow;
    std::vector<LayoutBox*> positioned;
    std::vector<LayoutBox*> positive;

    const auto bucket = [&](LayoutBox* child) {
        const ComputedStyle* style = child->style();
        if (!style || style->display == css::Display::None) {
            return;
        }
        if (paint::createsStackingContext(*child)) {
            const int order = paint::stackingOrder(*child);
            if (order < 0) {
                negative.push_back(child);
            } else if (order > 0) {
                positive.push_back(child);
            } else {
                positioned.push_back(child);
            }
            return;
        }
        if (paint::isPositioned(*child)) {
            positioned.push_back(child);
            return;
        }
        inFlow.push_back(child);
    };

    for (const std::unique_ptr<LayoutBox>& child : box.children()) {
        bucket(child.get());
    }
    const auto byOrder = [](const LayoutBox* a, const LayoutBox* b) {
        return paint::stackingOrder(*a) < paint::stackingOrder(*b);
    };
    std::stable_sort(negative.begin(), negative.end(), byOrder);
    std::stable_sort(positive.begin(), positive.end(), byOrder);

    // Reverse paint order: last painted is tested first.
    const auto testGroup = [&](const std::vector<LayoutBox*>& group) {
        for (auto it = group.rbegin(); it != group.rend(); ++it) {
            if (testBox(**it, px, py)) {
                return true;
            }
        }
        return false;
    };

    if (testGroup(positive) || testGroup(positioned)) {
        return true;
    }
    // Atomic inlines sit in the text of this box and paint after it.
    const auto& atomics = box.atomicInlines();
    for (auto it = atomics.rbegin(); it != atomics.rend(); ++it) {
        if (testBox(*it->get(), px, py)) {
            return true;
        }
    }
    return testGroup(inFlow) || testGroup(negative);
}

bool Tester::testBox(LayoutBox& box, float px, float py) {
    const ComputedStyle* style = box.style();
    if (!style || style->display == css::Display::None) {
        return false;
    }

    float localX = px;
    float localY = py;
    if (!style->transform.identity) {
        SkMatrix inverse;
        if (!paint::transformMatrix(box).invert(&inverse)) {
            return false; // degenerate transform: nothing is reachable
        }
        const SkPoint point = inverse.mapPoint(SkPoint{px, py});
        localX = point.x();
        localY = point.y();
    }

    // overflow: hidden clips descendants, including out-of-flow ones, so a point
    // outside the padding box cannot reach anything inside.
    if (style->clipsOverflow() && !paint::contains(paint::paddingBoxRRect(box), localX, localY)) {
        return false;
    }

    // Children live in the scrolled content space.
    if (testChildren(box, localX + box.scrollLeft(), localY + box.scrollTop())) {
        return true;
    }

    // The box itself, last: its own background paints below its children.
    if (!paint::contains(paint::borderBoxRRect(box), localX, localY)) {
        return false;
    }
    return record(box, localX, localY);
}

} // namespace

HitResult hitTest(LayoutBox& root, float x, float y) {
    Tester tester;
    tester.testBox(root, x, y);
    return tester.result;
}

} // namespace xgu::input
