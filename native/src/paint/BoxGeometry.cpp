#include "paint/BoxGeometry.h"

#include <algorithm>
#include <cmath>

namespace xgu::paint {
namespace {
// Resolves a border radius against the box it rounds.
float resolveRadius(const css::Length& radius, float boxWidth, float boxHeight, float fontSize) {
    if (radius.isPercent()) {
        // CSS resolves horizontal radii against the width and vertical ones
        // against the height; the engine keeps corners circular, so the smaller
        // dimension wins.
        return radius.value / 100.0f * std::min(boxWidth, boxHeight);
    }
    css::LengthContext context;
    context.fontSize = fontSize;
    context.rootFontSize = fontSize;
    return std::max(0.0f, css::resolveLength(radius, context, 0.0f));
}

} // namespace

SkRect toSkRect(const layout::Rect& rect) {
    return SkRect::MakeXYWH(rect.x, rect.y, rect.width, rect.height);
}

// Rounded rectangle of a box, shrunk by `inset` on every side (used for the
// inner edge of a border).
SkRRect roundedRect(const layout::Rect& box, const css::ComputedStyle& style, const float inset[4]) {
    SkRect rect = toSkRect(box);
    if (inset) {
        rect.fLeft += inset[css::kLeft];
        rect.fTop += inset[css::kTop];
        rect.fRight -= inset[css::kRight];
        rect.fBottom -= inset[css::kBottom];
        if (rect.fRight < rect.fLeft) {
            rect.fRight = rect.fLeft;
        }
        if (rect.fBottom < rect.fTop) {
            rect.fBottom = rect.fTop;
        }
    }
    if (!style.hasBorderRadius()) {
        return SkRRect::MakeRect(rect);
    }
    const float width = box.width;
    const float height = box.height;
    float radii[4];
    for (int corner = 0; corner < 4; ++corner) {
        radii[corner] = resolveRadius(style.borderRadius[static_cast<size_t>(corner)], width, height, style.fontSize);
        if (inset) {
            // The inner curve is the outer one minus the border it sits behind.
            const float shrink = corner == css::kTopLeft      ? std::min(inset[css::kTop], inset[css::kLeft])
                                 : corner == css::kTopRight   ? std::min(inset[css::kTop], inset[css::kRight])
                                 : corner == css::kBottomRight ? std::min(inset[css::kBottom], inset[css::kRight])
                                                                : std::min(inset[css::kBottom], inset[css::kLeft]);
            radii[corner] = std::max(0.0f, radii[corner] - shrink);
        }
    }
    SkVector corners[4] = {
        {radii[css::kTopLeft], radii[css::kTopLeft]},
        {radii[css::kTopRight], radii[css::kTopRight]},
        {radii[css::kBottomRight], radii[css::kBottomRight]},
        {radii[css::kBottomLeft], radii[css::kBottomLeft]},
    };
    SkRRect result;
    result.setRectRadii(rect, corners);
    return result;
}

SkRRect borderBoxRRect(const layout::LayoutBox& box) {
    return roundedRect(box.borderBox(), *box.style(), nullptr);
}

SkRRect paddingBoxRRect(const layout::LayoutBox& box) {
    const float inset[4] = {box.borderEdge(css::kTop), box.borderEdge(css::kRight), box.borderEdge(css::kBottom),
                            box.borderEdge(css::kLeft)};
    return roundedRect(box.borderBox(), *box.style(), inset);
}

bool contains(const SkRRect& rrect, float x, float y) {
    const SkRect& bounds = rrect.rect();
    if (x < bounds.fLeft || x > bounds.fRight || y < bounds.fTop || y > bounds.fBottom) {
        return false;
    }
    if (rrect.isRect() || rrect.isEmpty()) {
        return true;
    }
    // Inside the bounds: only the four corner ellipses can still exclude it.
    const SkRRect::Corner corners[4] = {SkRRect::kUpperLeft_Corner, SkRRect::kUpperRight_Corner,
                                        SkRRect::kLowerRight_Corner, SkRRect::kLowerLeft_Corner};
    for (const SkRRect::Corner corner : corners) {
        const SkVector radius = rrect.radii(corner);
        if (radius.fX <= 0.0f || radius.fY <= 0.0f) {
            continue;
        }
        const float centreX = corner == SkRRect::kUpperLeft_Corner || corner == SkRRect::kLowerLeft_Corner
                                  ? bounds.fLeft + radius.fX
                                  : bounds.fRight - radius.fX;
        const float centreY = corner == SkRRect::kUpperLeft_Corner || corner == SkRRect::kUpperRight_Corner
                                  ? bounds.fTop + radius.fY
                                  : bounds.fBottom - radius.fY;
        const bool inCornerQuadrant =
            (corner == SkRRect::kUpperLeft_Corner || corner == SkRRect::kLowerLeft_Corner ? x < centreX
                                                                                          : x > centreX) &&
            (corner == SkRRect::kUpperLeft_Corner || corner == SkRRect::kUpperRight_Corner ? y < centreY
                                                                                           : y > centreY);
        if (!inCornerQuadrant) {
            continue;
        }
        const float dx = (x - centreX) / radius.fX;
        const float dy = (y - centreY) / radius.fY;
        return dx * dx + dy * dy <= 1.0f;
    }
    return true;
}

// The transform matrix with its origin applied, ready for SkCanvas::concat.
SkMatrix transformMatrix(const layout::LayoutBox& box) {
    const css::ComputedStyle& style = *box.style();
    const layout::Rect& frame = box.borderBox();
    const auto axis = [&](size_t index, float extent) {
        const css::Length& length = style.transformOrigin[index];
        if (length.isPercent()) {
            return length.value / 100.0f * extent;
        }
        css::LengthContext context;
        context.fontSize = style.fontSize;
        context.rootFontSize = style.fontSize;
        return css::resolveLength(length, context, extent / 2.0f);
    };
    const float originX = frame.x + axis(0, frame.width);
    const float originY = frame.y + axis(1, frame.height);

    SkMatrix matrix = SkMatrix::MakeAll(style.transform.a, style.transform.c, style.transform.e, style.transform.b,
                                        style.transform.d, style.transform.f, 0.0f, 0.0f, 1.0f);
    SkMatrix result = SkMatrix::Translate(originX, originY);
    result.preConcat(matrix);
    result.preTranslate(-originX, -originY);
    return result;
}

bool isVisible(const layout::LayoutBox& box) {
    const css::ComputedStyle* style = box.style();
    return style && style->visibility == css::Visibility::Visible;
}

// z-index of a box for ordering; boxes that are not stacking contexts sort as 0.
int stackingOrder(const layout::LayoutBox& box) {
    const css::ComputedStyle* style = box.style();
    return style && !style->zIndexAuto ? style->zIndex : 0;
}

bool createsStackingContext(const layout::LayoutBox& box) {
    const css::ComputedStyle* style = box.style();
    return style && style->createsStackingContext();
}

bool isPositioned(const layout::LayoutBox& box) {
    const css::ComputedStyle* style = box.style();
    return style && style->isPositioned();
}

} // namespace xgu::paint
