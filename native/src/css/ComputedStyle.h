#pragma once

#include "core/Atom.h"
#include "core/RefCounted.h"
#include "css/CssValue.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xgu::css {

enum class Display : uint8_t { Block, Inline, InlineBlock, Flex, InlineFlex, None, Contents };
enum class PositionType : uint8_t { Static, Relative, Absolute, Fixed };
enum class BoxSizing : uint8_t { ContentBox, BorderBox };
enum class Overflow : uint8_t { Visible, Hidden, Scroll, Auto };
enum class Visibility : uint8_t { Visible, Hidden, Collapse };
enum class BorderStyle : uint8_t { None, Hidden, Solid, Dashed, Dotted, Double };
enum class FlexDirection : uint8_t { Row, RowReverse, Column, ColumnReverse };
enum class FlexWrap : uint8_t { NoWrap, Wrap, WrapReverse };
enum class Justify : uint8_t { FlexStart, FlexEnd, Center, SpaceBetween, SpaceAround, SpaceEvenly };
enum class Align : uint8_t { Auto, FlexStart, FlexEnd, Center, Stretch, Baseline, SpaceBetween, SpaceAround, SpaceEvenly };
enum class TextAlign : uint8_t { Start, Left, Right, Center, Justify };
enum class TextTransform : uint8_t { None, Uppercase, Lowercase, Capitalize };
enum class WhiteSpace : uint8_t { Normal, NoWrap, Pre, PreWrap, PreLine };
enum class TextOverflow : uint8_t { Clip, Ellipsis };
enum class FontStyle : uint8_t { Normal, Italic, Oblique };
enum class BackgroundRepeat : uint8_t { Repeat, NoRepeat, RepeatX, RepeatY };
enum class BackgroundSizeKind : uint8_t { Auto, Cover, Contain, Explicit };
enum class PointerEvents : uint8_t { Auto, None };

enum TextDecorationLine : uint8_t {
    kDecorationNone = 0,
    kDecorationUnderline = 1 << 0,
    kDecorationOverline = 1 << 1,
    kDecorationLineThrough = 1 << 2,
};

// Sides in CSS order: top, right, bottom, left.
enum Side : uint8_t { kTop = 0, kRight = 1, kBottom = 2, kLeft = 3 };
// Corners: top-left, top-right, bottom-right, bottom-left.
enum Corner : uint8_t { kTopLeft = 0, kTopRight = 1, kBottomRight = 2, kBottomLeft = 3 };

struct BoxShadow {
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float blur = 0.0f;
    float spread = 0.0f;
    Color color = Color::black();
    bool inset = false;

    bool operator==(const BoxShadow& other) const {
        return offsetX == other.offsetX && offsetY == other.offsetY && blur == other.blur &&
               spread == other.spread && color == other.color && inset == other.inset;
    }
};

// 2D affine transform as [a c e; b d f]; identity when `identity` is true.
struct Transform {
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, e = 0.0f, f = 0.0f;
    bool identity = true;

    void multiply(const Transform& other); // this = this * other
    bool operator==(const Transform& other) const {
        return identity == other.identity && a == other.a && b == other.b && c == other.c && d == other.d &&
               e == other.e && f == other.f;
    }
};

struct BackgroundSize {
    BackgroundSizeKind kind = BackgroundSizeKind::Auto;
    Length width = Length::automatic();
    Length height = Length::automatic();

    bool operator==(const BackgroundSize& other) const {
        return kind == other.kind && width == other.width && height == other.height;
    }
};

// The resolved values themselves, kept in a plain struct so a style can be
// copied (ComputedStyle is ref-counted and therefore non-copyable).
struct StyleValues {
    // --- box ---
    Display display = Display::Inline;
    PositionType position = PositionType::Static;
    std::array<Length, 4> inset{Length::automatic(), Length::automatic(), Length::automatic(), Length::automatic()};
    bool zIndexAuto = true;
    int zIndex = 0;
    Length width = Length::automatic();
    Length height = Length::automatic();
    Length minWidth = Length::automatic();
    Length minHeight = Length::automatic();
    Length maxWidth = Length::none();
    Length maxHeight = Length::none();
    std::array<Length, 4> margin{Length::zero(), Length::zero(), Length::zero(), Length::zero()};
    std::array<Length, 4> padding{Length::zero(), Length::zero(), Length::zero(), Length::zero()};
    std::array<float, 4> borderWidth{0.0f, 0.0f, 0.0f, 0.0f}; // used width (0 when style is none)
    std::array<BorderStyle, 4> borderStyle{BorderStyle::None, BorderStyle::None, BorderStyle::None, BorderStyle::None};
    std::array<Color, 4> borderColor{Color::black(), Color::black(), Color::black(), Color::black()};
    std::array<Length, 4> borderRadius{Length::zero(), Length::zero(), Length::zero(), Length::zero()};
    BoxSizing boxSizing = BoxSizing::ContentBox;
    Overflow overflowX = Overflow::Visible;
    Overflow overflowY = Overflow::Visible;

    // --- flex ---
    FlexDirection flexDirection = FlexDirection::Row;
    FlexWrap flexWrap = FlexWrap::NoWrap;
    Justify justifyContent = Justify::FlexStart;
    Align alignItems = Align::Stretch;
    Align alignSelf = Align::Auto;
    Align alignContent = Align::Stretch;
    float flexGrow = 0.0f;
    float flexShrink = 1.0f;
    Length flexBasis = Length::automatic();
    Length rowGap = Length::zero();
    Length columnGap = Length::zero();

    // --- painting ---
    Color backgroundColor = Color::transparent();
    std::string backgroundImage; // resolved url, empty for none
    BackgroundRepeat backgroundRepeat = BackgroundRepeat::Repeat;
    BackgroundSize backgroundSize;
    std::array<Length, 2> backgroundPosition{Length::percent(0.0f), Length::percent(0.0f)};
    float opacity = 1.0f;
    std::vector<BoxShadow> boxShadow;
    Visibility visibility = Visibility::Visible;
    Transform transform;
    std::array<Length, 2> transformOrigin{Length::percent(50.0f), Length::percent(50.0f)};
    PointerEvents pointerEvents = PointerEvents::Auto;
    Atom cursor;

    // --- text (inherited) ---
    Color color = Color::black();
    std::vector<std::string> fontFamily;
    float fontSize = 16.0f;
    int fontWeight = 400;
    FontStyle fontStyle = FontStyle::Normal;
    // Resolved line height in px; <= 0 means "normal" (font metrics decide).
    float lineHeight = -1.0f;
    float letterSpacing = 0.0f;
    TextAlign textAlign = TextAlign::Start;
    uint8_t textDecorationLine = kDecorationNone;
    Color textDecorationColor = Color::black();
    TextTransform textTransform = TextTransform::None;
    css::WhiteSpace whiteSpace = css::WhiteSpace::Normal;
    css::TextOverflow textOverflow = css::TextOverflow::Clip;
};

// Fully resolved style of one element. Immutable once produced by StyleEngine;
// shared through RefPtr so unchanged subtrees can keep their old instance.
class ComputedStyle : public RefCounted, public StyleValues {
public:
    ComputedStyle() = default;
    explicit ComputedStyle(const StyleValues& values) : StyleValues(values) {}

    void setValues(const StyleValues& values) { static_cast<StyleValues&>(*this) = values; }

    // --- helpers ---
    bool isDisplayNone() const { return display == Display::None; }
    bool isFlexContainer() const { return display == Display::Flex || display == Display::InlineFlex; }
    bool isInlineLevel() const { return display == Display::Inline || display == Display::InlineBlock ||
                                        display == Display::InlineFlex; }
    bool isPositioned() const { return position != PositionType::Static; }
    bool hasVisibleBorder() const;
    bool hasBorderRadius() const;
    bool createsStackingContext() const {
        return opacity < 1.0f || !transform.identity || (isPositioned() && !zIndexAuto);
    }
    bool clipsOverflow() const { return overflowX != Overflow::Visible || overflowY != Overflow::Visible; }
    float usedLineHeight() const { return lineHeight > 0.0f ? lineHeight : fontSize * 1.2f; }

    // Copies only the inherited properties from `parent`.
    void inheritFrom(const ComputedStyle& parent);

    // What changed between two styles, for invalidation.
    struct Diff {
        bool layout = false; // needs a new layout pass
        bool paint = false;  // repaint only
        bool inheritedChanged = false;
        bool any() const { return layout || paint || inheritedChanged; }
    };
    static Diff diff(const ComputedStyle& before, const ComputedStyle& after);

    // The style every element starts from (CSS initial values).
    static const ComputedStyle& initial();
};

} // namespace xgu::css
