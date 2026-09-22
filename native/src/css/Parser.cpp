// CSS parser for the engine's property subset: tokenizes, parses rules and
// declarations, expands shorthands and normalises values into CssValue.
//
// Deliberately not a full CSS parser (see docs/css-support.md): unknown
// properties, at-rules and unsupported values are dropped with a warning
// instead of being preserved.

#include "css/StyleSheet.h"

#include "css/Tokenizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <sstream>

namespace xgu::css {
namespace {

std::string lowered(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

uint8_t clampChannel(double value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
}

uint8_t alphaFromUnit(double value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

bool hexValue(char c, int& out) {
    if (c >= '0' && c <= '9') {
        out = c - '0';
    } else if (c >= 'a' && c <= 'f') {
        out = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        out = c - 'A' + 10;
    } else {
        return false;
    }
    return true;
}

bool parseHexColor(std::string_view hex, Color& out) {
    const auto component = [&](size_t index, bool doubled) -> int {
        int high = 0;
        int low = 0;
        if (doubled) {
            if (!hexValue(hex[index], high)) {
                return -1;
            }
            return high * 16 + high;
        }
        if (!hexValue(hex[index], high) || !hexValue(hex[index + 1], low)) {
            return -1;
        }
        return high * 16 + low;
    };
    int r = -1, g = -1, b = -1, a = 255;
    if (hex.size() == 3 || hex.size() == 4) {
        r = component(0, true);
        g = component(1, true);
        b = component(2, true);
        if (hex.size() == 4) {
            a = component(3, true);
        }
    } else if (hex.size() == 6 || hex.size() == 8) {
        r = component(0, false);
        g = component(2, false);
        b = component(4, false);
        if (hex.size() == 8) {
            a = component(6, false);
        }
    } else {
        return false;
    }
    if (r < 0 || g < 0 || b < 0 || a < 0) {
        return false;
    }
    out = Color::rgba(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b),
                      static_cast<uint8_t>(a));
    return true;
}

// hsl() with h in degrees and s/l in percent.
Color hslToRgb(double h, double s, double l, double alpha) {
    h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0) / 360.0;
    s = std::clamp(s, 0.0, 1.0);
    l = std::clamp(l, 0.0, 1.0);
    const auto hueToRgb = [](double p, double q, double t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
        if (t < 1.0 / 2.0) return q;
        if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        return p;
    };
    double r = l, g = l, b = l;
    if (s > 0.0) {
        const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
        const double p = 2.0 * l - q;
        r = hueToRgb(p, q, h + 1.0 / 3.0);
        g = hueToRgb(p, q, h);
        b = hueToRgb(p, q, h - 1.0 / 3.0);
    }
    return Color::rgba(clampChannel(r * 255.0), clampChannel(g * 255.0), clampChannel(b * 255.0),
                       alphaFromUnit(alpha));
}

// --- component values --------------------------------------------------------

// A flat list of the tokens that make up one declaration value, with whitespace
// collapsed and functions grouped.
struct ValueParser {
    std::vector<CssValue> components; // space separated
    std::vector<std::vector<CssValue>> groups; // comma separated groups of the above

    bool parse(std::string_view text);

private:
    bool parseGroup(Tokenizer& tokenizer, Token& token, std::vector<CssValue>& out);
    bool parseComponent(Tokenizer& tokenizer, Token& token, CssValue& out);
    bool parseFunction(Tokenizer& tokenizer, const std::string& name, CssValue& out);
};

Length lengthFromToken(const Token& token, bool& ok) {
    ok = true;
    if (token.type == TokenType::Number) {
        return Length::number(static_cast<float>(token.number));
    }
    if (token.type == TokenType::Percentage) {
        return Length::percent(static_cast<float>(token.number));
    }
    if (token.type == TokenType::Dimension) {
        const std::string unit = lowered(token.unit);
        const float value = static_cast<float>(token.number);
        if (unit == "px") return Length{value, LengthUnit::Px};
        if (unit == "em") return Length{value, LengthUnit::Em};
        if (unit == "rem") return Length{value, LengthUnit::Rem};
        if (unit == "vw") return Length{value, LengthUnit::Vw};
        if (unit == "vh") return Length{value, LengthUnit::Vh};
        if (unit == "vmin") return Length{value, LengthUnit::Vmin};
        if (unit == "vmax") return Length{value, LengthUnit::Vmax};
        // pt/pc/cm/mm/in are converted to px at the CSS reference of 96dpi.
        if (unit == "pt") return Length{value * 96.0f / 72.0f, LengthUnit::Px};
        if (unit == "pc") return Length{value * 16.0f, LengthUnit::Px};
        if (unit == "in") return Length{value * 96.0f, LengthUnit::Px};
        if (unit == "cm") return Length{value * 96.0f / 2.54f, LengthUnit::Px};
        if (unit == "mm") return Length{value * 96.0f / 25.4f, LengthUnit::Px};
        // Angles become plain numbers in degrees; only transform functions use them.
        if (unit == "deg") return Length::number(value);
        if (unit == "rad") return Length::number(value * 180.0f / 3.14159265358979323846f);
        if (unit == "grad") return Length::number(value * 0.9f);
        if (unit == "turn") return Length::number(value * 360.0f);
        // Times keep their own unit so a duration can be told from a count, and
        // they are normalised to seconds here.
        if (unit == "s") return Length{value, LengthUnit::Seconds};
        if (unit == "ms") return Length{value / 1000.0f, LengthUnit::Seconds};
    }
    ok = false;
    return Length::zero();
}

bool ValueParser::parseFunction(Tokenizer& tokenizer, const std::string& name, CssValue& out) {
    const std::string lowerName = lowered(name);
    std::vector<CssValue> arguments;
    Token token = tokenizer.nextSkippingSpace();
    while (token.type != TokenType::RightParen && token.type != TokenType::EndOfFile) {
        if (token.type == TokenType::Comma || token.type == TokenType::Delim) {
            token = tokenizer.nextSkippingSpace();
            continue;
        }
        CssValue argument;
        if (!parseComponent(tokenizer, token, argument)) {
            return false;
        }
        arguments.push_back(std::move(argument));
        token = tokenizer.nextSkippingSpace();
    }

    // Colour functions collapse into a Color value right away.
    if (lowerName == "rgb" || lowerName == "rgba") {
        if (arguments.size() < 3) {
            return false;
        }
        const auto channel = [&](size_t i) {
            const Length& length = arguments[i].length;
            return length.unit == LengthUnit::Percent ? clampChannel(length.value * 255.0 / 100.0)
                                                      : clampChannel(length.value);
        };
        uint8_t alpha = 255;
        if (arguments.size() >= 4) {
            const Length& length = arguments[3].length;
            alpha = length.unit == LengthUnit::Percent ? alphaFromUnit(length.value / 100.0) : alphaFromUnit(length.value);
        }
        out = CssValue::makeColor(Color::rgba(channel(0), channel(1), channel(2), alpha));
        return true;
    }
    if (lowerName == "hsl" || lowerName == "hsla") {
        if (arguments.size() < 3) {
            return false;
        }
        const double alpha = arguments.size() >= 4 ? (arguments[3].length.unit == LengthUnit::Percent
                                                          ? arguments[3].length.value / 100.0
                                                          : arguments[3].length.value)
                                                   : 1.0;
        out = CssValue::makeColor(hslToRgb(arguments[0].length.value, arguments[1].length.value / 100.0,
                                           arguments[2].length.value / 100.0, alpha));
        return true;
    }
    out = CssValue::makeFunction(Atom(lowerName), std::move(arguments));
    return true;
}

bool ValueParser::parseComponent(Tokenizer& tokenizer, Token& token, CssValue& out) {
    switch (token.type) {
    case TokenType::Ident: {
        const std::string name = lowered(token.value);
        Color color;
        if (namedColor(name, color)) {
            out = CssValue::makeColor(color);
            return true;
        }
        out = CssValue::makeKeyword(Atom(name));
        return true;
    }
    case TokenType::Hash: {
        Color color;
        if (!parseHexColor(token.value, color)) {
            return false;
        }
        out = CssValue::makeColor(color);
        return true;
    }
    case TokenType::Number:
    case TokenType::Percentage:
    case TokenType::Dimension: {
        bool ok = false;
        const Length length = lengthFromToken(token, ok);
        if (!ok) {
            return false;
        }
        out = CssValue::makeLength(length);
        return true;
    }
    case TokenType::String:
        out = CssValue::makeString(token.value);
        return true;
    case TokenType::Url:
        out = CssValue::makeUrl(token.value);
        return true;
    case TokenType::Function:
        return parseFunction(tokenizer, token.value, out);
    case TokenType::Delim:
        // '/' separates e.g. border-radius or background position/size; keep it
        // as a keyword so shorthand handlers can see it.
        out = CssValue::makeKeyword(Atom(token.value));
        return true;
    default:
        return false;
    }
}

bool ValueParser::parseGroup(Tokenizer& tokenizer, Token& token, std::vector<CssValue>& out) {
    while (token.type != TokenType::EndOfFile && token.type != TokenType::Comma) {
        CssValue component;
        if (!parseComponent(tokenizer, token, component)) {
            return false;
        }
        out.push_back(std::move(component));
        token = tokenizer.nextSkippingSpace();
    }
    return true;
}

bool ValueParser::parse(std::string_view text) {
    Tokenizer tokenizer(text);
    Token token = tokenizer.nextSkippingSpace();
    while (token.type != TokenType::EndOfFile) {
        std::vector<CssValue> group;
        if (!parseGroup(tokenizer, token, group)) {
            return false;
        }
        if (!group.empty()) {
            groups.push_back(std::move(group));
        }
        if (token.type == TokenType::Comma) {
            token = tokenizer.nextSkippingSpace();
        }
    }
    if (!groups.empty()) {
        components = groups.front();
    }
    return !groups.empty();
}

// --- per-property validation ---------------------------------------------------

const Atom& atomOf(const char* text) {
    // Interned once per distinct literal; cheap enough for a parser.
    static thread_local std::vector<std::pair<const char*, Atom>> cache;
    for (const auto& [key, atom] : cache) {
        if (key == text) {
            return atom;
        }
    }
    cache.emplace_back(text, Atom(text));
    return cache.back().second;
}

bool isKeywordIn(const CssValue& value, std::initializer_list<const char*> allowed) {
    if (!value.isKeyword()) {
        return false;
    }
    for (const char* candidate : allowed) {
        if (value.keyword == atomOf(candidate)) {
            return true;
        }
    }
    return false;
}

bool acceptsLength(PropertyId property, const CssValue& value, bool allowAuto, bool allowNegative) {
    if (value.isKeyword()) {
        if (allowAuto && value.keyword == atomOf("auto")) {
            return true;
        }
        return false;
    }
    if (!value.isLength()) {
        return false;
    }
    if (value.length.unit == LengthUnit::Number && value.length.value != 0.0f) {
        return false; // only 0 may be unitless
    }
    if (!allowNegative && value.length.value < 0.0f) {
        return false;
    }
    (void)property;
    return true;
}

// Normalises a single-value property; returns false when the value is invalid.
bool normalizeValue(PropertyId property, const std::vector<CssValue>& components, CssValue& out) {
    if (components.empty()) {
        return false;
    }
    const CssValue& first = components.front();
    const bool single = components.size() == 1;

    switch (property) {
    case PropertyId::Display:
        return single && isKeywordIn(first, {"block", "inline", "inline-block", "flex", "inline-flex", "none",
                                             "contents"}) &&
               (out = first, true);
    case PropertyId::Position:
        return single && isKeywordIn(first, {"static", "relative", "absolute", "fixed"}) && (out = first, true);
    case PropertyId::BoxSizing:
        return single && isKeywordIn(first, {"content-box", "border-box"}) && (out = first, true);
    case PropertyId::OverflowX:
    case PropertyId::OverflowY:
        return single && isKeywordIn(first, {"visible", "hidden", "scroll", "auto", "clip"}) && (out = first, true);
    case PropertyId::Visibility:
        return single && isKeywordIn(first, {"visible", "hidden", "collapse"}) && (out = first, true);
    case PropertyId::PointerEvents:
        return single && isKeywordIn(first, {"auto", "none"}) && (out = first, true);
    case PropertyId::Cursor:
        return single && first.isKeyword() && (out = first, true);

    case PropertyId::Top:
    case PropertyId::Right:
    case PropertyId::Bottom:
    case PropertyId::Left:
    case PropertyId::MarginTop:
    case PropertyId::MarginRight:
    case PropertyId::MarginBottom:
    case PropertyId::MarginLeft:
        return single && acceptsLength(property, first, true, true) && (out = first, true);
    case PropertyId::Width:
    case PropertyId::Height:
    case PropertyId::MinWidth:
    case PropertyId::MinHeight:
    case PropertyId::FlexBasis:
        return single && acceptsLength(property, first, true, false) && (out = first, true);
    case PropertyId::MaxWidth:
    case PropertyId::MaxHeight:
        if (single && isKeywordIn(first, {"none"})) {
            out = CssValue::makeLength(Length::none());
            return true;
        }
        return single && acceptsLength(property, first, false, false) && (out = first, true);
    case PropertyId::PaddingTop:
    case PropertyId::PaddingRight:
    case PropertyId::PaddingBottom:
    case PropertyId::PaddingLeft:
    case PropertyId::RowGap:
    case PropertyId::ColumnGap:
    case PropertyId::BorderTopLeftRadius:
    case PropertyId::BorderTopRightRadius:
    case PropertyId::BorderBottomRightRadius:
    case PropertyId::BorderBottomLeftRadius:
        return single && acceptsLength(property, first, false, false) && (out = first, true);

    case PropertyId::BorderTopWidth:
    case PropertyId::BorderRightWidth:
    case PropertyId::BorderBottomWidth:
    case PropertyId::BorderLeftWidth:
        if (single && first.isKeyword()) {
            if (first.keyword == atomOf("thin")) {
                out = CssValue::makeLength(Length::px(1.0f));
                return true;
            }
            if (first.keyword == atomOf("medium")) {
                out = CssValue::makeLength(Length::px(3.0f));
                return true;
            }
            if (first.keyword == atomOf("thick")) {
                out = CssValue::makeLength(Length::px(5.0f));
                return true;
            }
            return false;
        }
        return single && acceptsLength(property, first, false, false) && (out = first, true);
    case PropertyId::BorderTopStyle:
    case PropertyId::BorderRightStyle:
    case PropertyId::BorderBottomStyle:
    case PropertyId::BorderLeftStyle:
        return single && isKeywordIn(first, {"none", "hidden", "solid", "dashed", "dotted", "double"}) &&
               (out = first, true);

    case PropertyId::ZIndex:
        if (single && isKeywordIn(first, {"auto"})) {
            out = first;
            return true;
        }
        return single && first.isLength() && first.length.unit == LengthUnit::Number && (out = first, true);

    case PropertyId::FlexDirection:
        return single && isKeywordIn(first, {"row", "row-reverse", "column", "column-reverse"}) && (out = first, true);
    case PropertyId::FlexWrap:
        return single && isKeywordIn(first, {"nowrap", "wrap", "wrap-reverse"}) && (out = first, true);
    case PropertyId::JustifyContent:
        return single && isKeywordIn(first, {"flex-start", "start", "flex-end", "end", "center", "space-between",
                                             "space-around", "space-evenly"}) &&
               (out = first, true);
    case PropertyId::AlignItems:
    case PropertyId::AlignSelf:
        return single && isKeywordIn(first, {"auto", "flex-start", "start", "flex-end", "end", "center", "stretch",
                                             "baseline"}) &&
               (out = first, true);
    case PropertyId::AlignContent:
        return single && isKeywordIn(first, {"flex-start", "start", "flex-end", "end", "center", "stretch",
                                             "space-between", "space-around", "space-evenly"}) &&
               (out = first, true);
    case PropertyId::FlexGrow:
    case PropertyId::FlexShrink:
        return single && first.isLength() && first.length.unit == LengthUnit::Number && first.length.value >= 0.0f &&
               (out = first, true);

    case PropertyId::Color:
    case PropertyId::BackgroundColor:
    case PropertyId::BorderTopColor:
    case PropertyId::BorderRightColor:
    case PropertyId::BorderBottomColor:
    case PropertyId::BorderLeftColor:
    case PropertyId::TextDecorationColor:
        if (single && first.isKeyword() && first.keyword == atomOf("currentcolor")) {
            out = first;
            return true;
        }
        return single && first.isColor() && (out = first, true);

    case PropertyId::Opacity:
        if (single && first.isLength() && first.length.unit == LengthUnit::Percent) {
            out = CssValue::makeNumber(std::clamp(first.length.value / 100.0f, 0.0f, 1.0f));
            return true;
        }
        return single && first.isLength() && first.length.unit == LengthUnit::Number &&
               (out = CssValue::makeNumber(std::clamp(first.length.value, 0.0f, 1.0f)), true);

    case PropertyId::Transition:
    case PropertyId::Animation:
        // Kept as the raw component list; the cascade turns it into the typed
        // specs, where the timing functions and durations are resolved.
        if (single && isKeywordIn(first, {"none"})) {
            out = first;
            return true;
        }
        out = CssValue::makeList(components);
        return !components.empty();

    case PropertyId::BackgroundImage:
        if (single && isKeywordIn(first, {"none"})) {
            out = first;
            return true;
        }
        if (single && first.type == ValueType::Function &&
            (first.keyword.equalsIgnoringCase("linear-gradient") ||
             first.keyword.equalsIgnoringCase("radial-gradient"))) {
            out = first;
            return true;
        }
        return single && first.type == ValueType::Url && (out = first, true);
    case PropertyId::BackgroundRepeat:
        return isKeywordIn(first, {"repeat", "no-repeat", "repeat-x", "repeat-y", "space", "round"}) &&
               (out = first, true);
    case PropertyId::BackgroundSize:
        if (single && isKeywordIn(first, {"auto", "cover", "contain"})) {
            out = first;
            return true;
        }
        out = CssValue::makeList(components);
        return true;
    case PropertyId::BackgroundPosition:
    case PropertyId::TransformOrigin:
        out = CssValue::makeList(components);
        return true;

    case PropertyId::BoxShadow:
        if (single && isKeywordIn(first, {"none"})) {
            out = first;
            return true;
        }
        out = CssValue::makeList(components);
        return true;
    case PropertyId::Transform:
        if (single && isKeywordIn(first, {"none"})) {
            out = first;
            return true;
        }
        for (const CssValue& component : components) {
            if (component.type != ValueType::Function) {
                return false;
            }
        }
        out = CssValue::makeList(components);
        return true;

    case PropertyId::FontFamily:
        out = CssValue::makeList(components);
        return true;
    case PropertyId::FontSize:
        if (single && first.isKeyword()) {
            // Absolute keywords, relative to the 16px default.
            static const std::pair<const char*, float> kSizes[] = {
                {"xx-small", 9.0f}, {"x-small", 10.0f}, {"small", 13.0f},  {"medium", 16.0f},
                {"large", 18.0f},   {"x-large", 24.0f}, {"xx-large", 32.0f}};
            for (const auto& [name, size] : kSizes) {
                if (first.keyword == atomOf(name)) {
                    out = CssValue::makeLength(Length::px(size));
                    return true;
                }
            }
            return false;
        }
        return single && acceptsLength(property, first, false, false) && (out = first, true);
    case PropertyId::FontWeight:
        if (single && first.isKeyword()) {
            if (first.keyword == atomOf("normal")) {
                out = CssValue::makeNumber(400.0f);
                return true;
            }
            if (first.keyword == atomOf("bold")) {
                out = CssValue::makeNumber(700.0f);
                return true;
            }
            if (first.keyword == atomOf("lighter")) {
                out = CssValue::makeNumber(300.0f);
                return true;
            }
            if (first.keyword == atomOf("bolder")) {
                out = CssValue::makeNumber(600.0f);
                return true;
            }
            return false;
        }
        return single && first.isLength() && first.length.unit == LengthUnit::Number && (out = first, true);
    case PropertyId::FontStyle:
        return single && isKeywordIn(first, {"normal", "italic", "oblique"}) && (out = first, true);
    case PropertyId::LineHeight:
        if (single && isKeywordIn(first, {"normal"})) {
            out = first;
            return true;
        }
        return single && first.isLength() && (out = first, true);
    case PropertyId::LetterSpacing:
        if (single && isKeywordIn(first, {"normal"})) {
            out = CssValue::makeLength(Length::px(0.0f));
            return true;
        }
        return single && acceptsLength(property, first, false, true) && (out = first, true);
    case PropertyId::TextAlign:
        return single && isKeywordIn(first, {"left", "right", "center", "justify", "start", "end"}) &&
               (out = first, true);
    case PropertyId::TextDecorationLine:
        for (const CssValue& component : components) {
            if (!isKeywordIn(component, {"none", "underline", "overline", "line-through"})) {
                return false;
            }
        }
        out = CssValue::makeList(components);
        return true;
    case PropertyId::TextTransform:
        return single && isKeywordIn(first, {"none", "uppercase", "lowercase", "capitalize"}) && (out = first, true);
    case PropertyId::WhiteSpace:
        return single && isKeywordIn(first, {"normal", "nowrap", "pre", "pre-wrap", "pre-line"}) &&
               (out = first, true);
    case PropertyId::TextOverflow:
        return single && isKeywordIn(first, {"clip", "ellipsis"}) && (out = first, true);

    case PropertyId::Invalid:
    case PropertyId::Count:
        break;
    }
    return false;
}

// --- shorthands -----------------------------------------------------------------

void addDeclaration(DeclarationBlock& out, PropertyId property, CssValue value, bool important) {
    out.push_back(Declaration{property, std::move(value), important});
}

// margin / padding / border-width: 1-4 values in TRBL order.
bool expandBox(DeclarationBlock& out, const std::vector<CssValue>& components, bool important, PropertyId top,
               PropertyId right, PropertyId bottom, PropertyId left) {
    if (components.empty() || components.size() > 4) {
        return false;
    }
    std::array<CssValue, 4> sides; // top right bottom left
    switch (components.size()) {
    case 1:
        sides = {components[0], components[0], components[0], components[0]};
        break;
    case 2:
        sides = {components[0], components[1], components[0], components[1]};
        break;
    case 3:
        sides = {components[0], components[1], components[2], components[1]};
        break;
    default:
        sides = {components[0], components[1], components[2], components[3]};
        break;
    }
    const PropertyId ids[4] = {top, right, bottom, left};
    for (int i = 0; i < 4; ++i) {
        CssValue normalized;
        if (!normalizeValue(ids[i], {sides[static_cast<size_t>(i)]}, normalized)) {
            return false;
        }
        addDeclaration(out, ids[i], std::move(normalized), important);
    }
    return true;
}

bool expandBorderSide(DeclarationBlock& out, const std::vector<CssValue>& components, bool important,
                      PropertyId widthId, PropertyId styleId, PropertyId colorId) {
    CssValue width = CssValue::makeLength(Length::px(3.0f)); // "medium"
    CssValue style = CssValue::makeKeyword(Atom("none"));
    CssValue color = CssValue::makeKeyword(Atom("currentcolor"));
    bool sawWidth = false, sawStyle = false, sawColor = false;

    for (const CssValue& component : components) {
        CssValue normalized;
        if (!sawStyle && normalizeValue(styleId, {component}, normalized)) {
            style = normalized;
            sawStyle = true;
            continue;
        }
        if (!sawWidth && normalizeValue(widthId, {component}, normalized)) {
            width = normalized;
            sawWidth = true;
            continue;
        }
        if (!sawColor && normalizeValue(colorId, {component}, normalized)) {
            color = normalized;
            sawColor = true;
            continue;
        }
        return false;
    }
    if (!sawWidth && !sawStyle && !sawColor) {
        return false;
    }
    addDeclaration(out, widthId, std::move(width), important);
    addDeclaration(out, styleId, std::move(style), important);
    addDeclaration(out, colorId, std::move(color), important);
    return true;
}

bool expandFlex(DeclarationBlock& out, const std::vector<CssValue>& components, bool important) {
    if (components.size() == 1 && isKeywordIn(components[0], {"none"})) {
        addDeclaration(out, PropertyId::FlexGrow, CssValue::makeNumber(0.0f), important);
        addDeclaration(out, PropertyId::FlexShrink, CssValue::makeNumber(0.0f), important);
        addDeclaration(out, PropertyId::FlexBasis, CssValue::makeKeyword(Atom("auto")), important);
        return true;
    }
    if (components.size() == 1 && isKeywordIn(components[0], {"auto", "initial"})) {
        const bool isAuto = components[0].keyword == atomOf("auto");
        addDeclaration(out, PropertyId::FlexGrow, CssValue::makeNumber(isAuto ? 1.0f : 0.0f), important);
        addDeclaration(out, PropertyId::FlexShrink, CssValue::makeNumber(1.0f), important);
        addDeclaration(out, PropertyId::FlexBasis, CssValue::makeKeyword(Atom("auto")), important);
        return true;
    }

    float grow = 1.0f;
    float shrink = 1.0f;
    CssValue basis = CssValue::makeLength(Length::px(0.0f));
    bool sawGrow = false, sawShrink = false, sawBasis = false;

    for (const CssValue& component : components) {
        if (component.isLength() && component.length.unit == LengthUnit::Number && !sawBasis) {
            if (!sawGrow) {
                grow = component.length.value;
                sawGrow = true;
                continue;
            }
            if (!sawShrink) {
                shrink = component.length.value;
                sawShrink = true;
                continue;
            }
        }
        CssValue normalized;
        if (!sawBasis && normalizeValue(PropertyId::FlexBasis, {component}, normalized)) {
            basis = normalized;
            sawBasis = true;
            continue;
        }
        return false;
    }
    if (!sawGrow && !sawBasis) {
        return false;
    }
    addDeclaration(out, PropertyId::FlexGrow, CssValue::makeNumber(grow), important);
    addDeclaration(out, PropertyId::FlexShrink, CssValue::makeNumber(shrink), important);
    addDeclaration(out, PropertyId::FlexBasis, std::move(basis), important);
    return true;
}

bool expandBackground(DeclarationBlock& out, const std::vector<CssValue>& components, bool important) {
    // background: <color> | <url> | <repeat> | <position>/<size>
    CssValue color = CssValue::makeColor(Color::transparent());
    CssValue image = CssValue::makeKeyword(Atom("none"));
    CssValue repeat = CssValue::makeKeyword(Atom("repeat"));
    std::vector<CssValue> position;
    std::vector<CssValue> size;
    bool afterSlash = false;

    for (const CssValue& component : components) {
        if (component.isKeyword() && component.keyword == atomOf("/")) {
            afterSlash = true;
            continue;
        }
        if (component.isColor()) {
            color = component;
            continue;
        }
        if (component.type == ValueType::Url) {
            image = component;
            continue;
        }
        if (isKeywordIn(component, {"repeat", "no-repeat", "repeat-x", "repeat-y", "space", "round"})) {
            repeat = component;
            continue;
        }
        if (isKeywordIn(component, {"none"})) {
            image = component;
            continue;
        }
        if (afterSlash) {
            size.push_back(component);
            continue;
        }
        if (component.isLength() || isKeywordIn(component, {"left", "right", "top", "bottom", "center"})) {
            position.push_back(component);
            continue;
        }
        if (isKeywordIn(component, {"cover", "contain"})) {
            size.push_back(component);
            continue;
        }
        // Unknown background component (attachment, origin, clip): ignored.
    }

    addDeclaration(out, PropertyId::BackgroundColor, std::move(color), important);
    addDeclaration(out, PropertyId::BackgroundImage, std::move(image), important);
    addDeclaration(out, PropertyId::BackgroundRepeat, std::move(repeat), important);
    if (!position.empty()) {
        addDeclaration(out, PropertyId::BackgroundPosition, CssValue::makeList(position), important);
    }
    if (!size.empty()) {
        addDeclaration(out, PropertyId::BackgroundSize,
                       size.size() == 1 ? size.front() : CssValue::makeList(size), important);
    }
    return true;
}

bool expandTextDecoration(DeclarationBlock& out, const std::vector<CssValue>& components, bool important) {
    std::vector<CssValue> lines;
    CssValue color = CssValue::makeKeyword(Atom("currentcolor"));
    for (const CssValue& component : components) {
        if (component.isColor()) {
            color = component;
            continue;
        }
        if (isKeywordIn(component, {"none", "underline", "overline", "line-through"})) {
            lines.push_back(component);
            continue;
        }
        if (isKeywordIn(component, {"solid", "dashed", "dotted", "double", "wavy"})) {
            continue; // style is not rendered yet
        }
        return false;
    }
    if (lines.empty()) {
        lines.push_back(CssValue::makeKeyword(Atom("none")));
    }
    addDeclaration(out, PropertyId::TextDecorationLine, CssValue::makeList(lines), important);
    addDeclaration(out, PropertyId::TextDecorationColor, std::move(color), important);
    return true;
}

// Shorthand names the parser expands; everything else must be a longhand.
enum class Shorthand : uint8_t {
    None,
    Margin,
    Padding,
    BorderWidth,
    BorderStyle,
    BorderColor,
    BorderRadius,
    Border,
    BorderTop,
    BorderRight,
    BorderBottom,
    BorderLeft,
    Flex,
    FlexFlow,
    Gap,
    Inset,
    Overflow,
    Background,
    TextDecoration,
};

Shorthand shorthandFromName(std::string_view name) {
    if (name == "margin") return Shorthand::Margin;
    if (name == "padding") return Shorthand::Padding;
    if (name == "border-width") return Shorthand::BorderWidth;
    if (name == "border-style") return Shorthand::BorderStyle;
    if (name == "border-color") return Shorthand::BorderColor;
    if (name == "border-radius") return Shorthand::BorderRadius;
    if (name == "border") return Shorthand::Border;
    if (name == "border-top") return Shorthand::BorderTop;
    if (name == "border-right") return Shorthand::BorderRight;
    if (name == "border-bottom") return Shorthand::BorderBottom;
    if (name == "border-left") return Shorthand::BorderLeft;
    if (name == "flex") return Shorthand::Flex;
    if (name == "flex-flow") return Shorthand::FlexFlow;
    if (name == "gap") return Shorthand::Gap;
    if (name == "inset") return Shorthand::Inset;
    if (name == "overflow") return Shorthand::Overflow;
    if (name == "background") return Shorthand::Background;
    if (name == "text-decoration") return Shorthand::TextDecoration;
    return Shorthand::None;
}

bool expandShorthand(Shorthand shorthand, const std::vector<CssValue>& components, bool important,
                     DeclarationBlock& out) {
    switch (shorthand) {
    case Shorthand::Margin:
        return expandBox(out, components, important, PropertyId::MarginTop, PropertyId::MarginRight,
                         PropertyId::MarginBottom, PropertyId::MarginLeft);
    case Shorthand::Padding:
        return expandBox(out, components, important, PropertyId::PaddingTop, PropertyId::PaddingRight,
                         PropertyId::PaddingBottom, PropertyId::PaddingLeft);
    case Shorthand::Inset:
        return expandBox(out, components, important, PropertyId::Top, PropertyId::Right, PropertyId::Bottom,
                         PropertyId::Left);
    case Shorthand::BorderWidth:
        return expandBox(out, components, important, PropertyId::BorderTopWidth, PropertyId::BorderRightWidth,
                         PropertyId::BorderBottomWidth, PropertyId::BorderLeftWidth);
    case Shorthand::BorderStyle:
        return expandBox(out, components, important, PropertyId::BorderTopStyle, PropertyId::BorderRightStyle,
                         PropertyId::BorderBottomStyle, PropertyId::BorderLeftStyle);
    case Shorthand::BorderColor:
        return expandBox(out, components, important, PropertyId::BorderTopColor, PropertyId::BorderRightColor,
                         PropertyId::BorderBottomColor, PropertyId::BorderLeftColor);
    case Shorthand::BorderRadius:
        // The "/" form (elliptical corners) is not supported; take the first set.
        return expandBox(out, components, important, PropertyId::BorderTopLeftRadius,
                         PropertyId::BorderTopRightRadius, PropertyId::BorderBottomRightRadius,
                         PropertyId::BorderBottomLeftRadius);
    case Shorthand::Border:
        return expandBorderSide(out, components, important, PropertyId::BorderTopWidth, PropertyId::BorderTopStyle,
                                PropertyId::BorderTopColor) &&
               expandBorderSide(out, components, important, PropertyId::BorderRightWidth,
                                PropertyId::BorderRightStyle, PropertyId::BorderRightColor) &&
               expandBorderSide(out, components, important, PropertyId::BorderBottomWidth,
                                PropertyId::BorderBottomStyle, PropertyId::BorderBottomColor) &&
               expandBorderSide(out, components, important, PropertyId::BorderLeftWidth,
                                PropertyId::BorderLeftStyle, PropertyId::BorderLeftColor);
    case Shorthand::BorderTop:
        return expandBorderSide(out, components, important, PropertyId::BorderTopWidth, PropertyId::BorderTopStyle,
                                PropertyId::BorderTopColor);
    case Shorthand::BorderRight:
        return expandBorderSide(out, components, important, PropertyId::BorderRightWidth,
                                PropertyId::BorderRightStyle, PropertyId::BorderRightColor);
    case Shorthand::BorderBottom:
        return expandBorderSide(out, components, important, PropertyId::BorderBottomWidth,
                                PropertyId::BorderBottomStyle, PropertyId::BorderBottomColor);
    case Shorthand::BorderLeft:
        return expandBorderSide(out, components, important, PropertyId::BorderLeftWidth,
                                PropertyId::BorderLeftStyle, PropertyId::BorderLeftColor);
    case Shorthand::Flex:
        return expandFlex(out, components, important);
    case Shorthand::FlexFlow: {
        bool ok = false;
        for (const CssValue& component : components) {
            CssValue normalized;
            if (normalizeValue(PropertyId::FlexDirection, {component}, normalized)) {
                addDeclaration(out, PropertyId::FlexDirection, std::move(normalized), important);
                ok = true;
            } else if (normalizeValue(PropertyId::FlexWrap, {component}, normalized)) {
                addDeclaration(out, PropertyId::FlexWrap, std::move(normalized), important);
                ok = true;
            } else {
                return false;
            }
        }
        return ok;
    }
    case Shorthand::Gap: {
        if (components.empty() || components.size() > 2) {
            return false;
        }
        CssValue row;
        CssValue column;
        if (!normalizeValue(PropertyId::RowGap, {components[0]}, row)) {
            return false;
        }
        if (!normalizeValue(PropertyId::ColumnGap, {components.size() > 1 ? components[1] : components[0]}, column)) {
            return false;
        }
        addDeclaration(out, PropertyId::RowGap, std::move(row), important);
        addDeclaration(out, PropertyId::ColumnGap, std::move(column), important);
        return true;
    }
    case Shorthand::Overflow: {
        if (components.empty() || components.size() > 2) {
            return false;
        }
        CssValue x;
        CssValue y;
        if (!normalizeValue(PropertyId::OverflowX, {components[0]}, x)) {
            return false;
        }
        if (!normalizeValue(PropertyId::OverflowY, {components.size() > 1 ? components[1] : components[0]}, y)) {
            return false;
        }
        addDeclaration(out, PropertyId::OverflowX, std::move(x), important);
        addDeclaration(out, PropertyId::OverflowY, std::move(y), important);
        return true;
    }
    case Shorthand::Background:
        return expandBackground(out, components, important);
    case Shorthand::TextDecoration:
        return expandTextDecoration(out, components, important);
    case Shorthand::None:
        break;
    }
    return false;
}

// --- rule parsing ----------------------------------------------------------------

void addWarning(std::vector<std::string>* warnings, std::string message) {
    if (warnings && warnings->size() < 64) {
        warnings->push_back(std::move(message));
    }
}

// Splits "name: value; name2: value2" into declarations.
void parseDeclarationList(std::string_view text, DeclarationBlock& out, std::vector<std::string>* warnings) {
    size_t position = 0;
    while (position < text.size()) {
        // A declaration ends at ';' that is not inside a string or parentheses.
        size_t end = position;
        int depth = 0;
        char quote = '\0';
        while (end < text.size()) {
            const char c = text[end];
            if (quote != '\0') {
                if (c == quote) {
                    quote = '\0';
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                depth = std::max(0, depth - 1);
            } else if (c == ';' && depth == 0) {
                break;
            }
            ++end;
        }
        const std::string_view declaration = text.substr(position, end - position);
        position = end + 1;

        const size_t colon = declaration.find(':');
        if (colon == std::string_view::npos) {
            const std::string trimmed(declaration);
            if (trimmed.find_first_not_of(" \t\r\n\f") != std::string::npos) {
                addWarning(warnings, "declaration without ':' ignored: " + trimmed);
            }
            continue;
        }
        parseDeclaration(declaration.substr(0, colon), declaration.substr(colon + 1), out, warnings);
    }
}

std::string trim(std::string_view text) {
    const size_t first = text.find_first_not_of(" \t\r\n\f");
    if (first == std::string_view::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r\n\f");
    return std::string(text.substr(first, last - first + 1));
}

} // namespace

bool isShorthandName(std::string_view name) { return shorthandFromName(name) != Shorthand::None; }

bool parseDeclaration(std::string_view name, std::string_view valueText, DeclarationBlock& out,
                      std::vector<std::string>* warnings) {
    const std::string property = lowered(trim(name));
    std::string value = trim(valueText);
    if (property.empty() || value.empty()) {
        return false;
    }

    bool important = false;
    const size_t bang = value.rfind('!');
    if (bang != std::string::npos) {
        const std::string rest = lowered(trim(std::string_view(value).substr(bang + 1)));
        if (rest == "important") {
            important = true;
            value = trim(std::string_view(value).substr(0, bang));
        }
    }
    if (value.empty()) {
        return false;
    }

    // Custom properties (--x) and unsupported at-values are ignored by design.
    if (property.rfind("--", 0) == 0) {
        addWarning(warnings, "custom properties are not supported: " + property);
        return false;
    }

    ValueParser parsed;
    if (!parsed.parse(value)) {
        addWarning(warnings, "cannot parse value of " + property + ": " + value);
        return false;
    }

    // Global keywords apply to any property.
    if (parsed.components.size() == 1 && parsed.components[0].isKeyword()) {
        const Atom& keyword = parsed.components[0].keyword;
        if (keyword == atomOf("inherit") || keyword == atomOf("initial") || keyword == atomOf("unset") ||
            keyword == atomOf("revert")) {
            const PropertyId id = propertyFromName(property);
            if (id != PropertyId::Invalid) {
                addDeclaration(out, id, parsed.components[0], important);
                return true;
            }
        }
    }

    if (const Shorthand shorthand = shorthandFromName(property); shorthand != Shorthand::None) {
        DeclarationBlock expanded;
        if (!expandShorthand(shorthand, parsed.components, important, expanded)) {
            addWarning(warnings, "cannot parse shorthand " + property + ": " + value);
            return false;
        }
        out.insert(out.end(), std::make_move_iterator(expanded.begin()), std::make_move_iterator(expanded.end()));
        return true;
    }

    const PropertyId id = propertyFromName(property);
    if (id == PropertyId::Invalid) {
        addWarning(warnings, "unsupported property ignored: " + property);
        return false;
    }
    CssValue normalized;
    if (!normalizeValue(id, parsed.components, normalized)) {
        addWarning(warnings, "invalid value for " + property + ": " + value);
        return false;
    }
    addDeclaration(out, id, std::move(normalized), important);
    return true;
}

// @keyframes <name> { <selector> { <declarations> } ... } where a selector is a
// percentage, `from` or `to`, possibly several separated by commas.
void parseKeyframes(const std::string& name, std::string_view body, StyleSheet& sheet) {
    if (name.empty()) {
        return;
    }
    KeyframesRule rule;
    rule.name = Atom(name);

    size_t position = 0;
    while (position < body.size()) {
        const size_t brace = body.find('{', position);
        if (brace == std::string_view::npos) {
            break;
        }
        const std::string selectors = trim(body.substr(position, brace - position));
        size_t scan = brace + 1;
        int depth = 1;
        while (scan < body.size() && depth > 0) {
            if (body[scan] == '{') {
                ++depth;
            } else if (body[scan] == '}') {
                --depth;
            }
            ++scan;
        }
        const std::string_view declarationText =
            body.substr(brace + 1, (depth == 0 ? scan - 1 : body.size()) - brace - 1);
        position = scan;

        DeclarationBlock declarations = parseDeclarationBlock(declarationText, &sheet.warnings);
        if (declarations.empty()) {
            continue;
        }
        auto shared = std::make_shared<const DeclarationBlock>(std::move(declarations));

        size_t start = 0;
        while (start <= selectors.size()) {
            const size_t comma = selectors.find(',', start);
            const std::string piece =
                lowered(trim(selectors.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
            start = comma == std::string::npos ? selectors.size() + 1 : comma + 1;
            if (piece.empty()) {
                continue;
            }
            float offset = -1.0f;
            if (piece == "from") {
                offset = 0.0f;
            } else if (piece == "to") {
                offset = 1.0f;
            } else if (piece.back() == '%') {
                try {
                    offset = std::stof(piece.substr(0, piece.size() - 1)) / 100.0f;
                } catch (const std::exception&) {
                    offset = -1.0f;
                }
            }
            if (offset < 0.0f || offset > 1.0f) {
                addWarning(&sheet.warnings, "keyframe selector ignored: " + piece);
                continue;
            }
            rule.steps.push_back(KeyframeStep{offset, shared});
        }
    }

    if (rule.steps.empty()) {
        return;
    }
    std::stable_sort(rule.steps.begin(), rule.steps.end(),
                     [](const KeyframeStep& a, const KeyframeStep& b) { return a.offset < b.offset; });
    sheet.keyframes.push_back(std::move(rule));
}

DeclarationBlock parseDeclarationBlock(std::string_view text, std::vector<std::string>* warnings) {
    DeclarationBlock block;
    parseDeclarationList(text, block, warnings);
    return block;
}

StyleSheet parseStyleSheet(std::string_view css, Origin origin, uint32_t firstOrder) {
    StyleSheet sheet;
    uint32_t order = firstOrder;
    size_t position = 0;

    while (position < css.size()) {
        // Skip whitespace and comments before a rule.
        while (position < css.size()) {
            const char c = css[position];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
                ++position;
            } else if (c == '/' && position + 1 < css.size() && css[position + 1] == '*') {
                const size_t end = css.find("*/", position + 2);
                position = end == std::string_view::npos ? css.size() : end + 2;
            } else {
                break;
            }
        }
        if (position >= css.size()) {
            break;
        }

        // At-rules are skipped whole (media queries and friends are out of scope),
        // except @keyframes, which the animation engine needs.
        if (css[position] == '@') {
            const size_t brace = css.find('{', position);
            if (brace != std::string_view::npos) {
                const std::string prelude = trim(css.substr(position, brace - position));
                if (prelude.size() > 10 && lowered(prelude.substr(0, 10)) == "@keyframes") {
                    size_t scan = brace + 1;
                    int depth = 1;
                    while (scan < css.size() && depth > 0) {
                        if (css[scan] == '{') {
                            ++depth;
                        } else if (css[scan] == '}') {
                            --depth;
                        }
                        ++scan;
                    }
                    const std::string_view body =
                        css.substr(brace + 1, (depth == 0 ? scan - 1 : css.size()) - brace - 1);
                    parseKeyframes(trim(prelude.substr(10)), body, sheet);
                    position = scan;
                    continue;
                }
            }
            const size_t semicolon = css.find(';', position);
            if (semicolon != std::string_view::npos && (brace == std::string_view::npos || semicolon < brace)) {
                addWarning(&sheet.warnings, "at-rule ignored: " + trim(css.substr(position, semicolon - position)));
                position = semicolon + 1;
                continue;
            }
            if (brace == std::string_view::npos) {
                break;
            }
            int depth = 0;
            size_t scan = brace;
            for (; scan < css.size(); ++scan) {
                if (css[scan] == '{') {
                    ++depth;
                } else if (css[scan] == '}') {
                    if (--depth == 0) {
                        ++scan;
                        break;
                    }
                }
            }
            addWarning(&sheet.warnings, "at-rule ignored: " + trim(css.substr(position, brace - position)));
            position = scan;
            continue;
        }

        const size_t brace = css.find('{', position);
        if (brace == std::string_view::npos) {
            break;
        }
        const std::string selectorText = trim(css.substr(position, brace - position));

        // Find the matching close brace.
        size_t scan = brace + 1;
        int depth = 1;
        while (scan < css.size() && depth > 0) {
            if (css[scan] == '{') {
                ++depth;
            } else if (css[scan] == '}') {
                --depth;
            }
            ++scan;
        }
        const size_t bodyEnd = depth == 0 ? scan - 1 : css.size();
        const std::string_view body = css.substr(brace + 1, bodyEnd - brace - 1);
        position = depth == 0 ? scan : css.size();

        std::string selectorError;
        std::optional<SelectorList> selectors = SelectorList::parse(selectorText, &selectorError);
        if (!selectors) {
            addWarning(&sheet.warnings, "rule skipped, bad selector \"" + selectorText + "\": " + selectorError);
            continue;
        }

        DeclarationBlock declarations;
        parseDeclarationList(body, declarations, &sheet.warnings);
        if (declarations.empty()) {
            continue;
        }
        auto shared = std::make_shared<const DeclarationBlock>(std::move(declarations));
        for (Selector& selector : selectors->selectors) {
            StyleRule rule;
            rule.selector = std::move(selector);
            rule.declarations = shared;
            rule.order = order++;
            rule.origin = origin;
            sheet.rules.push_back(std::move(rule));
        }
    }
    return sheet;
}

std::string serializeValue(const CssValue& value) {
    std::ostringstream out;
    switch (value.type) {
    case ValueType::Keyword:
        out << value.keyword.string();
        break;
    case ValueType::Length: {
        const Length& length = value.length;
        if (length.isAuto()) {
            out << "auto";
            break;
        }
        if (length.isNone()) {
            out << "none";
            break;
        }
        // Trim trailing zeros for readability.
        std::ostringstream number;
        number << length.value;
        out << number.str();
        switch (length.unit) {
        case LengthUnit::Px:
            out << "px";
            break;
        case LengthUnit::Percent:
            out << "%";
            break;
        case LengthUnit::Em:
            out << "em";
            break;
        case LengthUnit::Rem:
            out << "rem";
            break;
        case LengthUnit::Vw:
            out << "vw";
            break;
        case LengthUnit::Vh:
            out << "vh";
            break;
        case LengthUnit::Vmin:
            out << "vmin";
            break;
        case LengthUnit::Vmax:
            out << "vmax";
            break;
        default:
            break;
        }
        break;
    }
    case ValueType::Color: {
        const Color& color = value.color;
        if (color.a == 255) {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", color.r, color.g, color.b);
            out << buffer;
        } else {
            out << "rgba(" << int(color.r) << ", " << int(color.g) << ", " << int(color.b) << ", "
                << (static_cast<float>(color.a) / 255.0f) << ")";
        }
        break;
    }
    case ValueType::String:
        out << '"' << value.text << '"';
        break;
    case ValueType::Url:
        out << "url(\"" << value.text << "\")";
        break;
    case ValueType::List:
        for (size_t i = 0; i < value.items.size(); ++i) {
            if (i > 0) {
                out << ' ';
            }
            out << serializeValue(value.items[i]);
        }
        break;
    case ValueType::Function:
        out << value.keyword.string() << '(';
        for (size_t i = 0; i < value.items.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << serializeValue(value.items[i]);
        }
        out << ')';
        break;
    case ValueType::Invalid:
        break;
    }
    return out.str();
}

std::string serializeDeclarations(const DeclarationBlock& block) {
    std::ostringstream out;
    for (size_t i = 0; i < block.size(); ++i) {
        if (i > 0) {
            out << ' ';
        }
        out << propertyName(block[i].property) << ": " << serializeValue(block[i].value);
        if (block[i].important) {
            out << " !important";
        }
        out << ';';
    }
    return out.str();
}

} // namespace xgu::css
