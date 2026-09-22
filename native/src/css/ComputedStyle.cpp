#include "css/ComputedStyle.h"

#include <cmath>

namespace xgu::css {

void Transform::multiply(const Transform& other) {
    if (other.identity) {
        return;
    }
    if (identity) {
        *this = other;
        return;
    }
    const float na = a * other.a + c * other.b;
    const float nb = b * other.a + d * other.b;
    const float nc = a * other.c + c * other.d;
    const float nd = b * other.c + d * other.d;
    const float ne = a * other.e + c * other.f + e;
    const float nf = b * other.e + d * other.f + f;
    a = na;
    b = nb;
    c = nc;
    d = nd;
    e = ne;
    f = nf;
    identity = false;
}

bool ComputedStyle::hasVisibleBorder() const {
    for (int i = 0; i < 4; ++i) {
        if (borderWidth[static_cast<size_t>(i)] > 0.0f &&
            borderStyle[static_cast<size_t>(i)] != BorderStyle::None &&
            borderStyle[static_cast<size_t>(i)] != BorderStyle::Hidden &&
            !borderColor[static_cast<size_t>(i)].isTransparent()) {
            return true;
        }
    }
    return false;
}

bool ComputedStyle::hasBorderRadius() const {
    for (const Length& radius : borderRadius) {
        if (!radius.isZero()) {
            return true;
        }
    }
    return false;
}

void ComputedStyle::inheritFrom(const ComputedStyle& parent) {
    color = parent.color;
    fontFamily = parent.fontFamily;
    fontSize = parent.fontSize;
    fontWeight = parent.fontWeight;
    fontStyle = parent.fontStyle;
    lineHeight = parent.lineHeight;
    letterSpacing = parent.letterSpacing;
    textAlign = parent.textAlign;
    textTransform = parent.textTransform;
    whiteSpace = parent.whiteSpace;
    visibility = parent.visibility;
    pointerEvents = parent.pointerEvents;
    cursor = parent.cursor;
}

const ComputedStyle& ComputedStyle::initial() {
    static const ComputedStyle* style = [] {
        auto* result = new ComputedStyle();
        result->fontFamily = {"Segoe UI", "Arial", "sans-serif"};
        return result;
    }();
    return *style;
}

ComputedStyle::Diff ComputedStyle::diff(const ComputedStyle& before, const ComputedStyle& after) {
    Diff result;

    const bool layoutChanged =
        before.display != after.display || before.position != after.position || before.inset != after.inset ||
        before.width != after.width || before.height != after.height || before.minWidth != after.minWidth ||
        before.minHeight != after.minHeight || before.maxWidth != after.maxWidth ||
        before.maxHeight != after.maxHeight || before.margin != after.margin || before.padding != after.padding ||
        before.borderWidth != after.borderWidth || before.boxSizing != after.boxSizing ||
        before.overflowX != after.overflowX || before.overflowY != after.overflowY ||
        before.flexDirection != after.flexDirection || before.flexWrap != after.flexWrap ||
        before.justifyContent != after.justifyContent || before.alignItems != after.alignItems ||
        before.alignSelf != after.alignSelf || before.alignContent != after.alignContent ||
        before.flexGrow != after.flexGrow || before.flexShrink != after.flexShrink ||
        before.flexBasis != after.flexBasis || before.rowGap != after.rowGap ||
        before.columnGap != after.columnGap || before.fontSize != after.fontSize ||
        before.fontWeight != after.fontWeight || before.fontStyle != after.fontStyle ||
        before.fontFamily != after.fontFamily || before.lineHeight != after.lineHeight ||
        before.letterSpacing != after.letterSpacing || before.textAlign != after.textAlign ||
        before.textTransform != after.textTransform || before.whiteSpace != after.whiteSpace ||
        before.textOverflow != after.textOverflow;
    result.layout = layoutChanged;

    result.paint = layoutChanged || before.backgroundColor != after.backgroundColor ||
                   before.backgroundImage != after.backgroundImage ||
                   !(before.backgroundGradient == after.backgroundGradient) ||
                   before.backgroundRepeat != after.backgroundRepeat ||
                   before.backgroundSize != after.backgroundSize ||
                   before.backgroundPosition != after.backgroundPosition || before.opacity != after.opacity ||
                   before.boxShadow != after.boxShadow || before.visibility != after.visibility ||
                   !(before.transform == after.transform) || before.transformOrigin != after.transformOrigin ||
                   before.color != after.color || before.borderColor != after.borderColor ||
                   before.borderStyle != after.borderStyle || before.borderRadius != after.borderRadius ||
                   before.textDecorationLine != after.textDecorationLine ||
                   before.textDecorationColor != after.textDecorationColor || before.zIndex != after.zIndex ||
                   before.zIndexAuto != after.zIndexAuto;

    result.inheritedChanged =
        before.color != after.color || before.fontFamily != after.fontFamily || before.fontSize != after.fontSize ||
        before.fontWeight != after.fontWeight || before.fontStyle != after.fontStyle ||
        before.lineHeight != after.lineHeight || before.letterSpacing != after.letterSpacing ||
        before.textAlign != after.textAlign || before.textTransform != after.textTransform ||
        before.whiteSpace != after.whiteSpace || before.visibility != after.visibility ||
        before.pointerEvents != after.pointerEvents || before.cursor != after.cursor;

    return result;
}

} // namespace xgu::css
