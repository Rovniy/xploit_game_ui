#pragma once

#include "core/Atom.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xgu::css {

// --- colors ------------------------------------------------------------------

// Straight (non-premultiplied) sRGB with 8-bit channels, as authored in CSS.
struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    static constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return Color{r, g, b, a}; }
    static constexpr Color transparent() { return Color{0, 0, 0, 0}; }
    static constexpr Color black() { return Color{0, 0, 0, 255}; }
    static constexpr Color white() { return Color{255, 255, 255, 255}; }

    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    bool operator!=(const Color& other) const { return !(*this == other); }
    bool isTransparent() const { return a == 0; }

    // 0xAARRGGBB, the layout Skia's SkColor expects.
    uint32_t toArgb() const {
        return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    }
};

// --- lengths -----------------------------------------------------------------

enum class LengthUnit : uint8_t {
    Px,
    Percent,
    Em,   // relative to this element's font-size
    Rem,  // relative to the root font-size
    Vw,
    Vh,
    Vmin,
    Vmax,
    Auto,
    None,    // "none" for max-width/max-height
    Number,  // unitless (line-height multiplier, flex-grow, opacity, z-index)
};

struct Length {
    float value = 0.0f;
    LengthUnit unit = LengthUnit::Px;

    static constexpr Length px(float v) { return Length{v, LengthUnit::Px}; }
    static constexpr Length percent(float v) { return Length{v, LengthUnit::Percent}; }
    static constexpr Length number(float v) { return Length{v, LengthUnit::Number}; }
    static constexpr Length automatic() { return Length{0.0f, LengthUnit::Auto}; }
    static constexpr Length none() { return Length{0.0f, LengthUnit::None}; }
    static constexpr Length zero() { return Length{0.0f, LengthUnit::Px}; }

    bool isAuto() const { return unit == LengthUnit::Auto; }
    bool isNone() const { return unit == LengthUnit::None; }
    bool isPercent() const { return unit == LengthUnit::Percent; }
    bool isPx() const { return unit == LengthUnit::Px; }
    bool isZero() const { return value == 0.0f && (unit == LengthUnit::Px || unit == LengthUnit::Percent); }

    bool operator==(const Length& other) const { return value == other.value && unit == other.unit; }
    bool operator!=(const Length& other) const { return !(*this == other); }
};

// Context needed to turn relative units into pixels.
struct LengthContext {
    float fontSize = 16.0f;     // this element's computed font-size
    float rootFontSize = 16.0f; // the root element's computed font-size
    float viewportWidth = 0.0f; // CSS pixels
    float viewportHeight = 0.0f;
};

// Resolves everything except percentages (which layout handles) and auto/none.
// `fallback` is returned for auto/none/percent.
float resolveLength(const Length& length, const LengthContext& context, float fallback = 0.0f);

// --- generic parsed value ------------------------------------------------------

enum class ValueType : uint8_t {
    Invalid,
    Keyword, // identifier such as "block", "center", "inherit"
    Length,
    Color,
    String, // quoted string
    Url,    // url(...)
    List,   // comma or space separated
    Function,
};

// Parse-time representation. The cascade turns these into the typed fields of
// ComputedStyle; nothing outside the CSS module keeps CssValue around.
struct CssValue {
    ValueType type = ValueType::Invalid;
    Atom keyword;              // Keyword, and the name for Function
    css::Length length;        // Length
    css::Color color;          // Color
    std::string text;          // String, Url
    std::vector<CssValue> items; // List, Function arguments

    bool valid() const { return type != ValueType::Invalid; }
    bool isKeyword() const { return type == ValueType::Keyword; }
    bool isKeyword(const Atom& name) const { return type == ValueType::Keyword && keyword == name; }
    bool isLength() const { return type == ValueType::Length; }
    bool isColor() const { return type == ValueType::Color; }

    static CssValue makeKeyword(const Atom& name) {
        CssValue value;
        value.type = ValueType::Keyword;
        value.keyword = name;
        return value;
    }
    static CssValue makeLength(css::Length length) {
        CssValue value;
        value.type = ValueType::Length;
        value.length = length;
        return value;
    }
    static CssValue makeNumber(float number) { return makeLength(css::Length::number(number)); }
    static CssValue makeColor(css::Color color) {
        CssValue value;
        value.type = ValueType::Color;
        value.color = color;
        return value;
    }
    static CssValue makeString(std::string text) {
        CssValue value;
        value.type = ValueType::String;
        value.text = std::move(text);
        return value;
    }
    static CssValue makeUrl(std::string url) {
        CssValue value;
        value.type = ValueType::Url;
        value.text = std::move(url);
        return value;
    }
    static CssValue makeList(std::vector<CssValue> items) {
        CssValue value;
        value.type = ValueType::List;
        value.items = std::move(items);
        return value;
    }
    static CssValue makeFunction(const Atom& name, std::vector<CssValue> arguments) {
        CssValue value;
        value.type = ValueType::Function;
        value.keyword = name;
        value.items = std::move(arguments);
        return value;
    }
};

// Named CSS colours the engine knows, plus "transparent" and "currentcolor".
// Returns false for unknown names.
bool namedColor(std::string_view name, Color& out);

} // namespace xgu::css
