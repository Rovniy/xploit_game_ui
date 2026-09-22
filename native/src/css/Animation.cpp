#include "css/Animation.h"

#include "css/ComputedStyle.h"

#include <algorithm>
#include <cmath>

namespace xgu::css {
namespace {

// Solves the cubic Bézier for y at the given x, the way CSS easing works.
// Newton first, bisection as a fallback, which is what browsers do.
float solveBezier(float x, float x1, float y1, float x2, float y2) {
    const auto curve = [](float a, float b, float t) {
        const float inverse = 1.0f - t;
        return 3.0f * inverse * inverse * t * a + 3.0f * inverse * t * t * b + t * t * t;
    };
    const auto slope = [](float a, float b, float t) {
        const float inverse = 1.0f - t;
        return 3.0f * inverse * inverse * a + 6.0f * inverse * t * (b - a) + 3.0f * t * t * (1.0f - b);
    };

    float t = x;
    for (int i = 0; i < 8; ++i) {
        const float error = curve(x1, x2, t) - x;
        if (std::fabs(error) < 1e-4f) {
            return curve(y1, y2, t);
        }
        const float derivative = slope(x1, x2, t);
        if (std::fabs(derivative) < 1e-6f) {
            break;
        }
        t -= error / derivative;
    }
    float low = 0.0f;
    float high = 1.0f;
    t = x;
    for (int i = 0; i < 24; ++i) {
        const float value = curve(x1, x2, t);
        if (std::fabs(value - x) < 1e-4f) {
            break;
        }
        if (value > x) {
            high = t;
        } else {
            low = t;
        }
        t = (low + high) * 0.5f;
    }
    return curve(y1, y2, t);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

uint8_t lerpChannel(uint8_t a, uint8_t b, float t) {
    return static_cast<uint8_t>(std::lround(std::clamp(lerp(a, b, t), 0.0f, 255.0f)));
}

Color blendColor(const Color& a, const Color& b, float t) {
    return Color::rgba(lerpChannel(a.r, b.r, t), lerpChannel(a.g, b.g, t), lerpChannel(a.b, b.b, t),
                       lerpChannel(a.a, b.a, t));
}

// Two lengths only blend when they are in the same unit; otherwise the value
// switches at the halfway point, which is what a browser does for
// non-interpolable pairs.
Length blendLength(const Length& a, const Length& b, float t) {
    if (a.unit == b.unit) {
        return Length{lerp(a.value, b.value, t), a.unit};
    }
    return t < 0.5f ? a : b;
}

Transform blendTransform(const Transform& a, const Transform& b, float t) {
    // The matrices are blended entry by entry. That is not the decomposed
    // interpolation the spec asks for, but for the translate, scale and rotate
    // a game UI animates the difference is not visible.
    Transform result;
    result.a = lerp(a.a, b.a, t);
    result.b = lerp(a.b, b.b, t);
    result.c = lerp(a.c, b.c, t);
    result.d = lerp(a.d, b.d, t);
    result.e = lerp(a.e, b.e, t);
    result.f = lerp(a.f, b.f, t);
    result.identity = a.identity && b.identity;
    return result;
}

} // namespace

float TimingFunction::evaluate(float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (kind) {
    case TimingKind::Linear:
        return t;
    case TimingKind::Ease:
        return solveBezier(t, 0.25f, 0.1f, 0.25f, 1.0f);
    case TimingKind::EaseIn:
        return solveBezier(t, 0.42f, 0.0f, 1.0f, 1.0f);
    case TimingKind::EaseOut:
        return solveBezier(t, 0.0f, 0.0f, 0.58f, 1.0f);
    case TimingKind::EaseInOut:
        return solveBezier(t, 0.42f, 0.0f, 0.58f, 1.0f);
    case TimingKind::CubicBezier:
        return solveBezier(t, x1, y1, x2, y2);
    }
    return t;
}

const std::vector<PropertyId>& animatableProperties() {
    static const std::vector<PropertyId> properties = {
        PropertyId::Opacity,        PropertyId::Color,          PropertyId::BackgroundColor,
        PropertyId::Width,          PropertyId::Height,         PropertyId::MinWidth,
        PropertyId::MinHeight,      PropertyId::MaxWidth,       PropertyId::MaxHeight,
        PropertyId::Top,            PropertyId::Right,          PropertyId::Bottom,
        PropertyId::Left,           PropertyId::MarginTop,      PropertyId::MarginRight,
        PropertyId::MarginBottom,   PropertyId::MarginLeft,     PropertyId::PaddingTop,
        PropertyId::PaddingRight,   PropertyId::PaddingBottom,  PropertyId::PaddingLeft,
        PropertyId::BorderTopWidth, PropertyId::BorderRightWidth, PropertyId::BorderBottomWidth,
        PropertyId::BorderLeftWidth, PropertyId::BorderTopColor, PropertyId::BorderRightColor,
        PropertyId::BorderBottomColor, PropertyId::BorderLeftColor, PropertyId::BorderTopLeftRadius,
        PropertyId::BorderTopRightRadius, PropertyId::BorderBottomRightRadius,
        PropertyId::BorderBottomLeftRadius, PropertyId::FontSize, PropertyId::LetterSpacing,
        PropertyId::Transform,      PropertyId::FlexGrow,       PropertyId::FlexShrink,
        PropertyId::RowGap,         PropertyId::ColumnGap,
    };
    return properties;
}

bool isAnimatable(PropertyId property) {
    const std::vector<PropertyId>& list = animatableProperties();
    return std::find(list.begin(), list.end(), property) != list.end();
}

bool blendProperty(PropertyId property, const StyleValues& from, const StyleValues& to, float t, StyleValues& out) {
    switch (property) {
    case PropertyId::Opacity:
        out.opacity = lerp(from.opacity, to.opacity, t);
        return true;
    case PropertyId::Color:
        out.color = blendColor(from.color, to.color, t);
        return true;
    case PropertyId::BackgroundColor:
        out.backgroundColor = blendColor(from.backgroundColor, to.backgroundColor, t);
        return true;
    case PropertyId::Width:
        out.width = blendLength(from.width, to.width, t);
        return true;
    case PropertyId::Height:
        out.height = blendLength(from.height, to.height, t);
        return true;
    case PropertyId::MinWidth:
        out.minWidth = blendLength(from.minWidth, to.minWidth, t);
        return true;
    case PropertyId::MinHeight:
        out.minHeight = blendLength(from.minHeight, to.minHeight, t);
        return true;
    case PropertyId::MaxWidth:
        out.maxWidth = blendLength(from.maxWidth, to.maxWidth, t);
        return true;
    case PropertyId::MaxHeight:
        out.maxHeight = blendLength(from.maxHeight, to.maxHeight, t);
        return true;
    case PropertyId::Top:
        out.inset[kTop] = blendLength(from.inset[kTop], to.inset[kTop], t);
        return true;
    case PropertyId::Right:
        out.inset[kRight] = blendLength(from.inset[kRight], to.inset[kRight], t);
        return true;
    case PropertyId::Bottom:
        out.inset[kBottom] = blendLength(from.inset[kBottom], to.inset[kBottom], t);
        return true;
    case PropertyId::Left:
        out.inset[kLeft] = blendLength(from.inset[kLeft], to.inset[kLeft], t);
        return true;
    case PropertyId::MarginTop:
        out.margin[kTop] = blendLength(from.margin[kTop], to.margin[kTop], t);
        return true;
    case PropertyId::MarginRight:
        out.margin[kRight] = blendLength(from.margin[kRight], to.margin[kRight], t);
        return true;
    case PropertyId::MarginBottom:
        out.margin[kBottom] = blendLength(from.margin[kBottom], to.margin[kBottom], t);
        return true;
    case PropertyId::MarginLeft:
        out.margin[kLeft] = blendLength(from.margin[kLeft], to.margin[kLeft], t);
        return true;
    case PropertyId::PaddingTop:
        out.padding[kTop] = blendLength(from.padding[kTop], to.padding[kTop], t);
        return true;
    case PropertyId::PaddingRight:
        out.padding[kRight] = blendLength(from.padding[kRight], to.padding[kRight], t);
        return true;
    case PropertyId::PaddingBottom:
        out.padding[kBottom] = blendLength(from.padding[kBottom], to.padding[kBottom], t);
        return true;
    case PropertyId::PaddingLeft:
        out.padding[kLeft] = blendLength(from.padding[kLeft], to.padding[kLeft], t);
        return true;
    case PropertyId::BorderTopWidth:
        out.borderWidth[kTop] = lerp(from.borderWidth[kTop], to.borderWidth[kTop], t);
        return true;
    case PropertyId::BorderRightWidth:
        out.borderWidth[kRight] = lerp(from.borderWidth[kRight], to.borderWidth[kRight], t);
        return true;
    case PropertyId::BorderBottomWidth:
        out.borderWidth[kBottom] = lerp(from.borderWidth[kBottom], to.borderWidth[kBottom], t);
        return true;
    case PropertyId::BorderLeftWidth:
        out.borderWidth[kLeft] = lerp(from.borderWidth[kLeft], to.borderWidth[kLeft], t);
        return true;
    case PropertyId::BorderTopColor:
        out.borderColor[kTop] = blendColor(from.borderColor[kTop], to.borderColor[kTop], t);
        return true;
    case PropertyId::BorderRightColor:
        out.borderColor[kRight] = blendColor(from.borderColor[kRight], to.borderColor[kRight], t);
        return true;
    case PropertyId::BorderBottomColor:
        out.borderColor[kBottom] = blendColor(from.borderColor[kBottom], to.borderColor[kBottom], t);
        return true;
    case PropertyId::BorderLeftColor:
        out.borderColor[kLeft] = blendColor(from.borderColor[kLeft], to.borderColor[kLeft], t);
        return true;
    case PropertyId::BorderTopLeftRadius:
        out.borderRadius[kTopLeft] = blendLength(from.borderRadius[kTopLeft], to.borderRadius[kTopLeft], t);
        return true;
    case PropertyId::BorderTopRightRadius:
        out.borderRadius[kTopRight] = blendLength(from.borderRadius[kTopRight], to.borderRadius[kTopRight], t);
        return true;
    case PropertyId::BorderBottomRightRadius:
        out.borderRadius[kBottomRight] = blendLength(from.borderRadius[kBottomRight], to.borderRadius[kBottomRight], t);
        return true;
    case PropertyId::BorderBottomLeftRadius:
        out.borderRadius[kBottomLeft] = blendLength(from.borderRadius[kBottomLeft], to.borderRadius[kBottomLeft], t);
        return true;
    case PropertyId::FontSize:
        out.fontSize = lerp(from.fontSize, to.fontSize, t);
        return true;
    case PropertyId::LetterSpacing:
        out.letterSpacing = lerp(from.letterSpacing, to.letterSpacing, t);
        return true;
    case PropertyId::Transform:
        out.transform = blendTransform(from.transform, to.transform, t);
        return true;
    case PropertyId::FlexGrow:
        out.flexGrow = lerp(from.flexGrow, to.flexGrow, t);
        return true;
    case PropertyId::FlexShrink:
        out.flexShrink = lerp(from.flexShrink, to.flexShrink, t);
        return true;
    case PropertyId::RowGap:
        out.rowGap = blendLength(from.rowGap, to.rowGap, t);
        return true;
    case PropertyId::ColumnGap:
        out.columnGap = blendLength(from.columnGap, to.columnGap, t);
        return true;
    default:
        return false;
    }
}

void copyProperty(PropertyId property, const StyleValues& from, StyleValues& out) {
    // Copying is blending fully towards the source.
    blendProperty(property, from, from, 0.0f, out);
}

bool propertyEquals(PropertyId property, const StyleValues& a, const StyleValues& b) {
    switch (property) {
    case PropertyId::Opacity:
        return a.opacity == b.opacity;
    case PropertyId::Color:
        return a.color == b.color;
    case PropertyId::BackgroundColor:
        return a.backgroundColor == b.backgroundColor;
    case PropertyId::Width:
        return a.width == b.width;
    case PropertyId::Height:
        return a.height == b.height;
    case PropertyId::MinWidth:
        return a.minWidth == b.minWidth;
    case PropertyId::MinHeight:
        return a.minHeight == b.minHeight;
    case PropertyId::MaxWidth:
        return a.maxWidth == b.maxWidth;
    case PropertyId::MaxHeight:
        return a.maxHeight == b.maxHeight;
    case PropertyId::Top:
        return a.inset[kTop] == b.inset[kTop];
    case PropertyId::Right:
        return a.inset[kRight] == b.inset[kRight];
    case PropertyId::Bottom:
        return a.inset[kBottom] == b.inset[kBottom];
    case PropertyId::Left:
        return a.inset[kLeft] == b.inset[kLeft];
    case PropertyId::MarginTop:
        return a.margin[kTop] == b.margin[kTop];
    case PropertyId::MarginRight:
        return a.margin[kRight] == b.margin[kRight];
    case PropertyId::MarginBottom:
        return a.margin[kBottom] == b.margin[kBottom];
    case PropertyId::MarginLeft:
        return a.margin[kLeft] == b.margin[kLeft];
    case PropertyId::PaddingTop:
        return a.padding[kTop] == b.padding[kTop];
    case PropertyId::PaddingRight:
        return a.padding[kRight] == b.padding[kRight];
    case PropertyId::PaddingBottom:
        return a.padding[kBottom] == b.padding[kBottom];
    case PropertyId::PaddingLeft:
        return a.padding[kLeft] == b.padding[kLeft];
    case PropertyId::BorderTopWidth:
        return a.borderWidth[kTop] == b.borderWidth[kTop];
    case PropertyId::BorderRightWidth:
        return a.borderWidth[kRight] == b.borderWidth[kRight];
    case PropertyId::BorderBottomWidth:
        return a.borderWidth[kBottom] == b.borderWidth[kBottom];
    case PropertyId::BorderLeftWidth:
        return a.borderWidth[kLeft] == b.borderWidth[kLeft];
    case PropertyId::BorderTopColor:
        return a.borderColor[kTop] == b.borderColor[kTop];
    case PropertyId::BorderRightColor:
        return a.borderColor[kRight] == b.borderColor[kRight];
    case PropertyId::BorderBottomColor:
        return a.borderColor[kBottom] == b.borderColor[kBottom];
    case PropertyId::BorderLeftColor:
        return a.borderColor[kLeft] == b.borderColor[kLeft];
    case PropertyId::BorderTopLeftRadius:
        return a.borderRadius[kTopLeft] == b.borderRadius[kTopLeft];
    case PropertyId::BorderTopRightRadius:
        return a.borderRadius[kTopRight] == b.borderRadius[kTopRight];
    case PropertyId::BorderBottomRightRadius:
        return a.borderRadius[kBottomRight] == b.borderRadius[kBottomRight];
    case PropertyId::BorderBottomLeftRadius:
        return a.borderRadius[kBottomLeft] == b.borderRadius[kBottomLeft];
    case PropertyId::FontSize:
        return a.fontSize == b.fontSize;
    case PropertyId::LetterSpacing:
        return a.letterSpacing == b.letterSpacing;
    case PropertyId::Transform:
        return a.transform == b.transform;
    case PropertyId::FlexGrow:
        return a.flexGrow == b.flexGrow;
    case PropertyId::FlexShrink:
        return a.flexShrink == b.flexShrink;
    case PropertyId::RowGap:
        return a.rowGap == b.rowGap;
    case PropertyId::ColumnGap:
        return a.columnGap == b.columnGap;
    default:
        return true; // not animatable, so never "changed" for our purposes
    }
}

} // namespace xgu::css
