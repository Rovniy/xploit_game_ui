#pragma once

#include "core/Atom.h"

#include <cstdint>
#include <string_view>

namespace xgu::css {

// Longhand properties the engine supports. Shorthands are expanded at parse
// time, so the cascade only ever sees these.
//
// Scope note (docs/css-support.md): this is the subset a game UI needs, not all
// of CSS. Anything outside it is dropped with a warning when EnableDebug is on.
enum class PropertyId : uint8_t {
    Invalid = 0,

    // box
    Display,
    Position,
    Top,
    Right,
    Bottom,
    Left,
    ZIndex,
    Width,
    Height,
    MinWidth,
    MinHeight,
    MaxWidth,
    MaxHeight,
    MarginTop,
    MarginRight,
    MarginBottom,
    MarginLeft,
    PaddingTop,
    PaddingRight,
    PaddingBottom,
    PaddingLeft,
    BorderTopWidth,
    BorderRightWidth,
    BorderBottomWidth,
    BorderLeftWidth,
    BorderTopStyle,
    BorderRightStyle,
    BorderBottomStyle,
    BorderLeftStyle,
    BorderTopColor,
    BorderRightColor,
    BorderBottomColor,
    BorderLeftColor,
    BorderTopLeftRadius,
    BorderTopRightRadius,
    BorderBottomRightRadius,
    BorderBottomLeftRadius,
    BoxSizing,
    OverflowX,
    OverflowY,

    // flexbox
    FlexDirection,
    FlexWrap,
    JustifyContent,
    AlignItems,
    AlignSelf,
    AlignContent,
    FlexGrow,
    FlexShrink,
    FlexBasis,
    RowGap,
    ColumnGap,

    // painting
    BackgroundColor,
    BackgroundImage,
    BackgroundSize,
    BackgroundPosition,
    BackgroundRepeat,
    Color,
    Opacity,
    BoxShadow,
    Visibility,
    Transform,
    TransformOrigin,
    PointerEvents,
    Cursor,

    // text (inherited)
    FontFamily,
    FontSize,
    FontWeight,
    FontStyle,
    LineHeight,
    LetterSpacing,
    TextAlign,
    TextDecorationLine,
    TextDecorationColor,
    TextTransform,
    WhiteSpace,
    TextOverflow,

    // Animation. The list-valued ones hold a whole transition or animation each,
    // because the cascade needs them as one unit.
    Transition,
    Animation,

    Count,
};

constexpr size_t kPropertyCount = static_cast<size_t>(PropertyId::Count);

struct PropertyMeta {
    std::string_view name;
    bool inherited = false;
    // Changing this property needs a new layout pass (otherwise paint suffices).
    bool affectsLayout = true;
};

const PropertyMeta& propertyMeta(PropertyId id);
// Case-insensitive; returns PropertyId::Invalid for unknown names.
PropertyId propertyFromName(std::string_view name);
std::string_view propertyName(PropertyId id);
bool propertyIsInherited(PropertyId id);
bool propertyAffectsLayout(PropertyId id);

} // namespace xgu::css
