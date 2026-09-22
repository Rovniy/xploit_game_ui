#pragma once

#include "css/ComputedStyle.h"
#include "layout/LayoutBox.h"

#include <include/core/SkMatrix.h>
#include <include/core/SkRRect.h>
#include <include/core/SkRect.h>

// Geometry shared by painting and hit testing. Both have to agree on where a box
// is, which corners are rounded, what a transform does and in which order boxes
// stack, so the rules live in one place and neither can drift from the other.
namespace xgu::paint {

SkRect toSkRect(const layout::Rect& rect);

// Rounded rectangle of `box`, shrunk by `inset` on every side when given (the
// inner edge of a border).
SkRRect roundedRect(const layout::Rect& box, const css::ComputedStyle& style, const float inset[4]);
SkRRect borderBoxRRect(const layout::LayoutBox& box);
SkRRect paddingBoxRRect(const layout::LayoutBox& box);

// True when the point is inside the rounded rectangle. SkRRect::contains only
// answers for whole rectangles, and hit testing asks about a point.
bool contains(const SkRRect& rrect, float x, float y);

// The box's transform with transform-origin applied, ready for SkCanvas::concat.
SkMatrix transformMatrix(const layout::LayoutBox& box);

bool isVisible(const layout::LayoutBox& box);
// z-index used for ordering; boxes that are not stacking contexts sort as 0.
int stackingOrder(const layout::LayoutBox& box);
bool createsStackingContext(const layout::LayoutBox& box);
bool isPositioned(const layout::LayoutBox& box);

} // namespace xgu::paint
