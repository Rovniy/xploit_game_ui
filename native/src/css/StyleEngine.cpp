#include "css/StyleEngine.h"

#include "core/AssetLoader.h"
#include "core/Log.h"
#include "dom/Element.h"

#include <algorithm>
#include <cmath>

namespace xgu::css {
namespace {

constexpr std::string_view kUserAgentCss = R"CSS(
html { display: block; font-family: "Segoe UI", Arial, sans-serif; font-size: 16px; color: #000000; }
body { display: block; margin: 8px; }
div, p, ul, ol, li, h1, h2, h3, h4, h5, h6, header, footer, section, nav, main, article, aside { display: block; }
span, label, a, strong, b, em, i, small, code { display: inline; }
head, meta, link, style, script, title { display: none; }
[hidden] { display: none; }
p { margin-top: 1em; margin-bottom: 1em; }
ul, ol { margin-top: 1em; margin-bottom: 1em; padding-left: 40px; }
h1 { font-size: 2em; font-weight: bold; margin-top: 0.67em; margin-bottom: 0.67em; }
h2 { font-size: 1.5em; font-weight: bold; margin-top: 0.83em; margin-bottom: 0.83em; }
h3 { font-size: 1.17em; font-weight: bold; margin-top: 1em; margin-bottom: 1em; }
h4 { font-weight: bold; margin-top: 1.33em; margin-bottom: 1.33em; }
h5 { font-size: 0.83em; font-weight: bold; }
h6 { font-size: 0.67em; font-weight: bold; }
strong, b { font-weight: bold; }
em, i { font-style: italic; }
small { font-size: 0.8em; }
a { text-decoration: underline; }
img { display: inline-block; }
button {
  display: inline-block; box-sizing: border-box; padding: 2px 8px;
  border: 1px solid #767676; border-radius: 2px; background-color: #efefef;
  color: #000000; font-size: 13.333px; text-align: center; white-space: nowrap; cursor: pointer;
}
input {
  display: inline-block; box-sizing: border-box; width: 150px; padding: 1px 2px;
  border: 1px solid #767676; border-radius: 2px; background-color: #ffffff;
  color: #000000; font-size: 13.333px; white-space: pre; overflow: hidden; cursor: text;
}
textarea {
  display: inline-block; box-sizing: border-box; width: 300px; height: 60px; padding: 2px;
  border: 1px solid #767676; background-color: #ffffff; color: #000000;
  font-size: 13.333px; white-space: pre-wrap; overflow: hidden; cursor: text;
}
button:focus, input:focus, textarea:focus { border-color: #0078d4; }
button[disabled], input[disabled], textarea[disabled] { color: #808080; background-color: #f0f0f0; cursor: auto; }
)CSS";

const Atom& atomFor(const char* name) {
    static thread_local std::vector<std::pair<const char*, Atom>> cache;
    for (const auto& [key, atom] : cache) {
        if (key == name) {
            return atom;
        }
    }
    cache.emplace_back(name, Atom(name));
    return cache.back().second;
}

bool keywordIs(const CssValue& value, const char* name) {
    return value.isKeyword() && value.keyword == atomFor(name);
}

// --- value -> typed field conversions ---------------------------------------

TimingFunction toTimingFunction(const CssValue& value) {
    TimingFunction timing;
    if (value.type == ValueType::Function && value.keyword.equalsIgnoringCase("cubic-bezier") &&
        value.items.size() >= 4) {
        timing.kind = TimingKind::CubicBezier;
        timing.x1 = value.items[0].length.value;
        timing.y1 = value.items[1].length.value;
        timing.x2 = value.items[2].length.value;
        timing.y2 = value.items[3].length.value;
        return timing;
    }
    if (!value.isKeyword()) {
        return timing;
    }
    if (value.keyword.equalsIgnoringCase("linear")) timing.kind = TimingKind::Linear;
    else if (value.keyword.equalsIgnoringCase("ease-in")) timing.kind = TimingKind::EaseIn;
    else if (value.keyword.equalsIgnoringCase("ease-out")) timing.kind = TimingKind::EaseOut;
    else if (value.keyword.equalsIgnoringCase("ease-in-out")) timing.kind = TimingKind::EaseInOut;
    else timing.kind = TimingKind::Ease;
    return timing;
}

bool isTimingKeyword(const CssValue& value) {
    if (value.type == ValueType::Function) {
        return value.keyword.equalsIgnoringCase("cubic-bezier");
    }
    return value.isKeyword() &&
           (value.keyword.equalsIgnoringCase("linear") || value.keyword.equalsIgnoringCase("ease") ||
            value.keyword.equalsIgnoringCase("ease-in") || value.keyword.equalsIgnoringCase("ease-out") ||
            value.keyword.equalsIgnoringCase("ease-in-out"));
}

bool isTime(const CssValue& value) { return value.isLength() && value.length.unit == LengthUnit::Seconds; }

// transition: <property> <duration> [<timing>] [<delay>], ...
//
// The parser drops the commas, which is fine: a property name always starts the
// next entry and nothing else in this syntax is a bare property name.
std::vector<TransitionSpec> toTransitions(const CssValue& value) {
    std::vector<TransitionSpec> result;
    const std::vector<CssValue>& items = value.type == ValueType::List ? value.items
                                                                       : std::vector<CssValue>{};
    std::vector<CssValue> single;
    const std::vector<CssValue>* list = &items;
    if (value.type != ValueType::List) {
        single.push_back(value);
        list = &single;
    }

    for (const CssValue& item : *list) {
        const bool startsEntry =
            item.isKeyword() && !isTimingKeyword(item) &&
            (item.keyword.equalsIgnoringCase("all") || propertyFromName(item.keyword.view()) !=
                                                            PropertyId::Invalid);
        if (startsEntry || result.empty()) {
            TransitionSpec spec;
            if (item.isKeyword() && item.keyword.equalsIgnoringCase("all")) {
                spec.all = true;
            } else if (startsEntry) {
                spec.property = propertyFromName(item.keyword.view());
            } else {
                spec.all = true; // a duration with no property means "all"
            }
            result.push_back(spec);
            if (startsEntry) {
                continue;
            }
        }
        TransitionSpec& spec = result.back();
        if (isTime(item)) {
            // The first time is the duration, the second the delay.
            if (spec.durationSeconds == 0.0f) {
                spec.durationSeconds = item.length.value;
            } else {
                spec.delaySeconds = item.length.value;
            }
        } else if (isTimingKeyword(item)) {
            spec.timing = toTimingFunction(item);
        }
    }

    // Entries with no duration animate nothing.
    result.erase(std::remove_if(result.begin(), result.end(),
                                [](const TransitionSpec& spec) { return spec.durationSeconds <= 0.0f; }),
                 result.end());
    return result;
}

// animation: <name> <duration> [<timing>] [<delay>] [<count>] [<direction>] [<fill>]
AnimationSpec toAnimation(const CssValue& value) {
    AnimationSpec spec;
    std::vector<CssValue> single;
    const std::vector<CssValue>* list = &value.items;
    if (value.type != ValueType::List) {
        single.push_back(value);
        list = &single;
    }

    for (const CssValue& item : *list) {
        if (isTime(item)) {
            if (spec.durationSeconds == 0.0f) {
                spec.durationSeconds = item.length.value;
            } else {
                spec.delaySeconds = item.length.value;
            }
            continue;
        }
        if (isTimingKeyword(item)) {
            spec.timing = toTimingFunction(item);
            continue;
        }
        if (item.isLength() && item.length.unit == LengthUnit::Number) {
            spec.iterations = item.length.value;
            continue;
        }
        if (!item.isKeyword()) {
            continue;
        }
        const Atom& word = item.keyword;
        if (word.equalsIgnoringCase("infinite")) {
            spec.iterations = -1.0f;
        } else if (word.equalsIgnoringCase("normal")) {
            spec.direction = AnimationDirection::Normal;
        } else if (word.equalsIgnoringCase("reverse")) {
            spec.direction = AnimationDirection::Reverse;
        } else if (word.equalsIgnoringCase("alternate")) {
            spec.direction = AnimationDirection::Alternate;
        } else if (word.equalsIgnoringCase("alternate-reverse")) {
            spec.direction = AnimationDirection::AlternateReverse;
        } else if (word.equalsIgnoringCase("forwards")) {
            spec.fill = AnimationFillMode::Forwards;
        } else if (word.equalsIgnoringCase("backwards")) {
            spec.fill = AnimationFillMode::Backwards;
        } else if (word.equalsIgnoringCase("both")) {
            spec.fill = AnimationFillMode::Both;
        } else if (word.equalsIgnoringCase("none")) {
            // `none` as a fill mode; as a name it would mean no animation, and
            // an empty name already says that.
        } else if (spec.name.view().empty()) {
            spec.name = word; // the first unrecognised keyword is the name
        }
    }
    return spec;
}

// "to right", "to bottom left", ... as the CSS angle they stand for. Returns the
// number of keywords consumed, or zero when this is not a side specification.
size_t toSideAngle(const std::vector<CssValue>& items, size_t first, float& outDegrees) {
    bool top = false, bottom = false, left = false, right = false;
    size_t i = first;
    for (; i < items.size() && items[i].isKeyword(); ++i) {
        const Atom& word = items[i].keyword;
        if (word.equalsIgnoringCase("top")) top = true;
        else if (word.equalsIgnoringCase("bottom")) bottom = true;
        else if (word.equalsIgnoringCase("left")) left = true;
        else if (word.equalsIgnoringCase("right")) right = true;
        else break;
    }
    if (top && right) outDegrees = 45.0f;
    else if (bottom && right) outDegrees = 135.0f;
    else if (bottom && left) outDegrees = 225.0f;
    else if (top && left) outDegrees = 315.0f;
    else if (top) outDegrees = 0.0f;
    else if (right) outDegrees = 90.0f;
    else if (bottom) outDegrees = 180.0f;
    else if (left) outDegrees = 270.0f;
    else return 0;
    return i - first;
}

// linear-gradient([<angle> | to <side>,] <color> [<position>], ...)
// radial-gradient([circle | ellipse,] <color> [<position>], ...)
//
// The parser drops the commas, which costs nothing here: in this syntax a colour
// always starts the next stop, and only the head can be an angle or a keyword.
// The size and position keywords of radial-gradient beyond the shape are
// ignored; a game UI wants a glow from the middle, and the full syntax would be
// a lot of surface for that.
Gradient toGradient(const CssValue& value, const ComputedStyle& style) {
    Gradient gradient;
    if (value.keyword.equalsIgnoringCase("linear-gradient")) {
        gradient.kind = GradientKind::Linear;
    } else if (value.keyword.equalsIgnoringCase("radial-gradient")) {
        gradient.kind = GradientKind::Radial;
    } else {
        return {};
    }

    const std::vector<CssValue>& items = value.items;
    size_t i = 0;
    if (!items.empty()) {
        float degrees = 0.0f;
        if (items[0].isLength() && items[0].length.unit == LengthUnit::Number) {
            // Angles reach us already converted to degrees.
            gradient.angleDegrees = items[0].length.value;
            i = 1;
        } else if (items[0].isKeyword() && items[0].keyword.equalsIgnoringCase("to")) {
            const size_t consumed = toSideAngle(items, 1, degrees);
            if (consumed > 0) {
                gradient.angleDegrees = degrees;
                i = 1 + consumed;
            }
        } else if (gradient.kind == GradientKind::Radial && items[0].isKeyword() &&
                   (items[0].keyword.equalsIgnoringCase("circle") ||
                    items[0].keyword.equalsIgnoringCase("ellipse"))) {
            i = 1;
        }
    }

    for (; i < items.size(); ++i) {
        const CssValue& item = items[i];
        GradientStop stop;
        if (keywordIs(item, "currentcolor")) {
            stop.color = style.color;
        } else if (item.type == ValueType::Color) {
            stop.color = item.color;
        } else {
            continue; // not a colour: skip it rather than reject the gradient
        }
        if (i + 1 < items.size() && items[i + 1].isLength() &&
            (items[i + 1].length.unit == LengthUnit::Percent || items[i + 1].length.unit == LengthUnit::Px)) {
            const Length& position = items[i + 1].length;
            // A px position needs the gradient line's length, which is not known
            // here, so only percentages are honoured.
            if (position.unit == LengthUnit::Percent) {
                stop.position = position.value / 100.0f;
            }
            ++i;
        }
        gradient.stops.push_back(stop);
    }

    // Stops without a position spread evenly between the ones that have one.
    if (!gradient.stops.empty()) {
        if (gradient.stops.front().position < 0.0f) {
            gradient.stops.front().position = 0.0f;
        }
        if (gradient.stops.back().position < 0.0f) {
            gradient.stops.back().position = 1.0f;
        }
        for (size_t k = 1; k + 1 < gradient.stops.size(); ++k) {
            if (gradient.stops[k].position >= 0.0f) {
                continue;
            }
            size_t next = k + 1;
            while (next < gradient.stops.size() && gradient.stops[next].position < 0.0f) {
                ++next;
            }
            const float from = gradient.stops[k - 1].position;
            const float to = next < gradient.stops.size() ? gradient.stops[next].position : 1.0f;
            const size_t gaps = next - k + 1;
            for (size_t j = k; j < next; ++j) {
                gradient.stops[j].position =
                    from + (to - from) * static_cast<float>(j - k + 1) / static_cast<float>(gaps);
            }
            k = next - 1;
        }
        // Positions never go backwards, as CSS requires.
        for (size_t k = 1; k < gradient.stops.size(); ++k) {
            gradient.stops[k].position = std::max(gradient.stops[k].position, gradient.stops[k - 1].position);
        }
    }
    return gradient.valid() ? gradient : Gradient{};
}

Display toDisplay(const CssValue& value) {
    if (keywordIs(value, "block")) return Display::Block;
    if (keywordIs(value, "inline")) return Display::Inline;
    if (keywordIs(value, "inline-block")) return Display::InlineBlock;
    if (keywordIs(value, "flex")) return Display::Flex;
    if (keywordIs(value, "inline-flex")) return Display::InlineFlex;
    if (keywordIs(value, "none")) return Display::None;
    if (keywordIs(value, "contents")) return Display::Contents;
    return Display::Inline;
}

PositionType toPosition(const CssValue& value) {
    if (keywordIs(value, "relative")) return PositionType::Relative;
    if (keywordIs(value, "absolute")) return PositionType::Absolute;
    if (keywordIs(value, "fixed")) return PositionType::Fixed;
    return PositionType::Static;
}

Overflow toOverflow(const CssValue& value) {
    if (keywordIs(value, "hidden") || keywordIs(value, "clip")) return Overflow::Hidden;
    if (keywordIs(value, "scroll")) return Overflow::Scroll;
    if (keywordIs(value, "auto")) return Overflow::Auto;
    return Overflow::Visible;
}

BorderStyle toBorderStyle(const CssValue& value) {
    if (keywordIs(value, "solid")) return BorderStyle::Solid;
    if (keywordIs(value, "dashed")) return BorderStyle::Dashed;
    if (keywordIs(value, "dotted")) return BorderStyle::Dotted;
    if (keywordIs(value, "double")) return BorderStyle::Double;
    if (keywordIs(value, "hidden")) return BorderStyle::Hidden;
    return BorderStyle::None;
}

FlexDirection toFlexDirection(const CssValue& value) {
    if (keywordIs(value, "row-reverse")) return FlexDirection::RowReverse;
    if (keywordIs(value, "column")) return FlexDirection::Column;
    if (keywordIs(value, "column-reverse")) return FlexDirection::ColumnReverse;
    return FlexDirection::Row;
}

Justify toJustify(const CssValue& value) {
    if (keywordIs(value, "flex-end") || keywordIs(value, "end")) return Justify::FlexEnd;
    if (keywordIs(value, "center")) return Justify::Center;
    if (keywordIs(value, "space-between")) return Justify::SpaceBetween;
    if (keywordIs(value, "space-around")) return Justify::SpaceAround;
    if (keywordIs(value, "space-evenly")) return Justify::SpaceEvenly;
    return Justify::FlexStart;
}

Align toAlign(const CssValue& value) {
    if (keywordIs(value, "flex-start") || keywordIs(value, "start")) return Align::FlexStart;
    if (keywordIs(value, "flex-end") || keywordIs(value, "end")) return Align::FlexEnd;
    if (keywordIs(value, "center")) return Align::Center;
    if (keywordIs(value, "baseline")) return Align::Baseline;
    if (keywordIs(value, "stretch")) return Align::Stretch;
    if (keywordIs(value, "space-between")) return Align::SpaceBetween;
    if (keywordIs(value, "space-around")) return Align::SpaceAround;
    if (keywordIs(value, "space-evenly")) return Align::SpaceEvenly;
    return Align::Auto;
}

TextAlign toTextAlign(const CssValue& value) {
    if (keywordIs(value, "left")) return TextAlign::Left;
    if (keywordIs(value, "right")) return TextAlign::Right;
    if (keywordIs(value, "center")) return TextAlign::Center;
    if (keywordIs(value, "justify")) return TextAlign::Justify;
    if (keywordIs(value, "end")) return TextAlign::Right;
    return TextAlign::Start;
}

WhiteSpace toWhiteSpace(const CssValue& value) {
    if (keywordIs(value, "nowrap")) return WhiteSpace::NoWrap;
    if (keywordIs(value, "pre")) return WhiteSpace::Pre;
    if (keywordIs(value, "pre-wrap")) return WhiteSpace::PreWrap;
    if (keywordIs(value, "pre-line")) return WhiteSpace::PreLine;
    return WhiteSpace::Normal;
}

BackgroundRepeat toBackgroundRepeat(const CssValue& value) {
    if (keywordIs(value, "no-repeat")) return BackgroundRepeat::NoRepeat;
    if (keywordIs(value, "repeat-x")) return BackgroundRepeat::RepeatX;
    if (keywordIs(value, "repeat-y")) return BackgroundRepeat::RepeatY;
    return BackgroundRepeat::Repeat;
}

float degreesOf(const Length& length, const char* unit) {
    // The tokenizer keeps deg/rad/turn as unknown dimensions, so transforms use
    // plain numbers plus the unit recorded in the function name handling below.
    (void)unit;
    return length.value;
}

// Builds a 2D transform from the parsed function list.
Transform toTransform(const CssValue& value, const LengthContext& context) {
    Transform result;
    if (value.type != ValueType::List) {
        return result;
    }
    for (const CssValue& function : value.items) {
        if (function.type != ValueType::Function) {
            continue;
        }
        const std::string name = function.keyword.string();
        const auto argument = [&](size_t index, float fallback = 0.0f) -> float {
            if (index >= function.items.size() || !function.items[index].isLength()) {
                return fallback;
            }
            const Length& length = function.items[index].length;
            if (length.unit == LengthUnit::Number || length.unit == LengthUnit::Percent) {
                return length.value;
            }
            return resolveLength(length, context, 0.0f);
        };
        Transform step;
        if (name == "translate" || name == "translatex" || name == "translatey") {
            step.identity = false;
            if (name == "translatey") {
                step.f = argument(0);
            } else {
                step.e = argument(0);
                step.f = name == "translate" ? argument(1) : 0.0f;
            }
        } else if (name == "scale" || name == "scalex" || name == "scaley") {
            step.identity = false;
            const float x = argument(0, 1.0f);
            if (name == "scalex") {
                step.a = x;
            } else if (name == "scaley") {
                step.d = x;
            } else {
                step.a = x;
                step.d = function.items.size() > 1 ? argument(1, 1.0f) : x;
            }
        } else if (name == "rotate" || name == "rotatez") {
            step.identity = false;
            float degrees = 0.0f;
            if (!function.items.empty() && function.items[0].isLength()) {
                const Length& length = function.items[0].length;
                degrees = degreesOf(length, "deg");
            }
            const float radians = degrees * 3.14159265358979323846f / 180.0f;
            step.a = std::cos(radians);
            step.b = std::sin(radians);
            step.c = -std::sin(radians);
            step.d = std::cos(radians);
        } else if (name == "skewx" || name == "skewy" || name == "skew") {
            step.identity = false;
            const float x = argument(0) * 3.14159265358979323846f / 180.0f;
            const float y = (name == "skew" && function.items.size() > 1 ? argument(1) : 0.0f) *
                            3.14159265358979323846f / 180.0f;
            if (name == "skewy") {
                step.b = std::tan(x);
            } else {
                step.c = std::tan(x);
                step.b = std::tan(y);
            }
        } else if (name == "matrix" && function.items.size() >= 6) {
            step.identity = false;
            step.a = argument(0, 1.0f);
            step.b = argument(1);
            step.c = argument(2);
            step.d = argument(3, 1.0f);
            step.e = argument(4);
            step.f = argument(5);
        } else {
            continue; // 3D transforms and perspective are out of scope
        }
        result.multiply(step);
    }
    return result;
}

std::vector<BoxShadow> toBoxShadows(const CssValue& value, const LengthContext& context, const Color& currentColor) {
    std::vector<BoxShadow> shadows;
    if (value.type != ValueType::List) {
        return shadows;
    }
    BoxShadow shadow;
    shadow.color = currentColor;
    int lengthIndex = 0;
    bool any = false;
    for (const CssValue& component : value.items) {
        if (keywordIs(component, "inset")) {
            shadow.inset = true;
            any = true;
            continue;
        }
        if (component.isColor()) {
            shadow.color = component.color;
            any = true;
            continue;
        }
        if (component.isLength()) {
            const float px = resolveLength(component.length, context, 0.0f);
            switch (lengthIndex++) {
            case 0:
                shadow.offsetX = px;
                break;
            case 1:
                shadow.offsetY = px;
                break;
            case 2:
                shadow.blur = std::max(0.0f, px);
                break;
            case 3:
                shadow.spread = px;
                break;
            default:
                break;
            }
            any = true;
        }
    }
    if (any && lengthIndex >= 2) {
        shadows.push_back(shadow);
    }
    return shadows;
}

std::vector<std::string> toFontFamilies(const CssValue& value) {
    std::vector<std::string> families;
    const auto push = [&](const CssValue& item) {
        if (item.type == ValueType::String) {
            families.push_back(item.text);
        } else if (item.isKeyword()) {
            families.push_back(item.keyword.string());
        }
    };
    if (value.type == ValueType::List) {
        // Space separated parts of one unquoted family name join back together.
        std::string current;
        for (const CssValue& item : value.items) {
            if (item.type == ValueType::String) {
                if (!current.empty()) {
                    families.push_back(current);
                    current.clear();
                }
                families.push_back(item.text);
                continue;
            }
            if (!item.isKeyword()) {
                continue;
            }
            if (!current.empty()) {
                current.push_back(' ');
            }
            current += item.keyword.string();
        }
        if (!current.empty()) {
            families.push_back(current);
        }
    } else {
        push(value);
    }
    return families;
}

} // namespace

std::string_view userAgentStyleSheet() { return kUserAgentCss; }

// --- RuleIndex ---------------------------------------------------------------

void RuleIndex::note(const Selector& selector) {
    for (const CompoundSelector& compound : selector.compounds) {
        for (const PseudoSelector& pseudo : compound.pseudos) {
            switch (pseudo.kind) {
            case PseudoClass::Hover:
                usesHover_ = true;
                break;
            case PseudoClass::Active:
                usesActive_ = true;
                break;
            case PseudoClass::Focus:
            case PseudoClass::FocusWithin:
                usesFocus_ = true;
                break;
            default:
                break;
            }
        }
    }
}

void RuleIndex::add(const StyleRule& rule) {
    if (rule.selector.empty()) {
        return;
    }
    ++ruleCount_;
    note(rule.selector);
    const CompoundSelector& rightmost = rule.selector.rightmost();
    if (!rightmost.id.empty()) {
        byId_[rightmost.id].push_back(&rule);
        return;
    }
    if (!rightmost.classes.empty()) {
        byClass_[rightmost.classes.front()].push_back(&rule);
        return;
    }
    if (!rightmost.tagName.empty()) {
        byTag_[rightmost.tagName].push_back(&rule);
        return;
    }
    universal_.push_back(&rule);
}

void RuleIndex::clear() {
    byId_.clear();
    byClass_.clear();
    byTag_.clear();
    universal_.clear();
    ruleCount_ = 0;
    usesHover_ = usesActive_ = usesFocus_ = false;
}

void RuleIndex::collect(const dom::Element& element, std::vector<const StyleRule*>& out) const {
    if (!element.id().empty()) {
        if (const auto it = byId_.find(element.id()); it != byId_.end()) {
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }
    for (const Atom& className : element.classList()) {
        if (const auto it = byClass_.find(className); it != byClass_.end()) {
            out.insert(out.end(), it->second.begin(), it->second.end());
        }
    }
    if (const auto it = byTag_.find(element.tagName()); it != byTag_.end()) {
        out.insert(out.end(), it->second.begin(), it->second.end());
    }
    out.insert(out.end(), universal_.begin(), universal_.end());
}

// --- StyleEngine ---------------------------------------------------------------

StyleEngine::StyleEngine(dom::Document& document) : document_(document) {
    userAgentSheet_ = parseStyleSheet(kUserAgentCss, Origin::UserAgent, 0);
    nextRuleOrder_ = static_cast<uint32_t>(userAgentSheet_.rules.size());
    document_.setMutationSink(this);
}

StyleEngine::~StyleEngine() {
    if (document_.mutationSink() == this) {
        document_.setMutationSink(nullptr);
    }
}

void StyleEngine::addStyleSheet(std::string_view css, Origin origin) {
    StyleSheet sheet = parseStyleSheet(css, origin, nextRuleOrder_);
    nextRuleOrder_ += static_cast<uint32_t>(sheet.rules.size()) + 1;
    for (const std::string& warning : sheet.warnings) {
        if (warnings_.size() < 64) {
            warnings_.push_back(warning);
        }
        XGU_LOG_WARNING("css: %s", warning.c_str());
    }
    authorSheets_.push_back(std::move(sheet));
    indexDirty_ = true;
    forceFullRecalc_ = true;
    collectKeyframes();
}

void StyleEngine::clearAuthorStyleSheets() {
    authorSheets_.clear();
    warnings_.clear();
    nextRuleOrder_ = static_cast<uint32_t>(userAgentSheet_.rules.size());
    indexDirty_ = true;
    forceFullRecalc_ = true;
    collectKeyframes();
}

void StyleEngine::reloadStyleSheets() {
    clearAuthorStyleSheets();
    IAssetLoader* loader = document_.assetLoader();

    for (dom::Node* node = &document_; node; node = dom::nextInTreeOrder(node, &document_)) {
        if (!node->isElement()) {
            continue;
        }
        auto& element = static_cast<dom::Element&>(*node);
        if (element.knownTag() == html::HtmlTag::Style) {
            addStyleSheet(element.textContent());
            continue;
        }
        if (element.knownTag() != html::HtmlTag::Link) {
            continue;
        }
        const std::string rel = element.getAttributeOrEmpty(atomFor("rel"));
        if (!Atom(rel).equalsIgnoringCase("stylesheet")) {
            continue;
        }
        const std::string href = element.getAttributeOrEmpty(atomFor("href"));
        if (href.empty()) {
            continue;
        }
        if (!loader) {
            XGU_LOG_ERROR("css: <link href=\"%s\"> needs a UI root", href.c_str());
            continue;
        }
        const std::optional<std::string> resolved = loader->resolve(document_.url(), href);
        if (!resolved) {
            XGU_LOG_ERROR("css: <link href=\"%s\"> was rejected", href.c_str());
            continue;
        }
        const std::optional<std::string> contents = loader->read(*resolved);
        if (!contents) {
            XGU_LOG_ERROR("css: cannot read stylesheet \"%s\"", resolved->c_str());
            continue;
        }
        addStyleSheet(*contents);
    }
    collectKeyframes();
}

void StyleEngine::collectKeyframes() {
    std::vector<KeyframesRule> all;
    for (const KeyframesRule& rule : userAgentSheet_.keyframes) {
        all.push_back(rule);
    }
    for (const StyleSheet& sheet : authorSheets_) {
        for (const KeyframesRule& rule : sheet.keyframes) {
            all.push_back(rule);
        }
    }
    animator_.setKeyframes(std::move(all));
}

void StyleEngine::rebuildIndex() {
    index_.clear();
    for (const StyleRule& rule : userAgentSheet_.rules) {
        index_.add(rule);
    }
    for (const StyleSheet& sheet : authorSheets_) {
        for (const StyleRule& rule : sheet.rules) {
            index_.add(rule);
        }
    }
    indexDirty_ = false;
}

void StyleEngine::invalidateAll() { forceFullRecalc_ = true; }

void StyleEngine::onNodeInserted(dom::Node& node) {
    if (dom::Element* parent = node.parentElement()) {
        parent->markDirty(dom::kDirtyStyleChildren);
    }
    node.markDirty(dom::kDirtyStyleSelf | dom::kDirtyStyleChildren);
}

void StyleEngine::onNodeRemoved(dom::Node& node, dom::Node& formerParent) {
    if (node.isElement()) {
        animator_.forget(static_cast<dom::Element&>(node));
    }
    formerParent.markDirty(dom::kDirtyStyleChildren | dom::kDirtyLayoutTree);
}

void StyleEngine::onAttributeChanged(dom::Element& element, const Atom& name) {
    element.markDirty(dom::kDirtyStyleSelf | dom::kDirtyStyleChildren);
    (void)name;
}

void StyleEngine::onTextChanged(dom::CharacterData& node) {
    node.markDirty(dom::kDirtyLayout | dom::kDirtyPaintSelf);
    if (dom::Element* parent = node.parentElement()) {
        parent->markDirty(dom::kDirtyLayout);
    }
}

RefPtr<ComputedStyle> StyleEngine::computeStyle(dom::Element& element, const ComputedStyle& parentStyle,
                                                const DeclarationBlock* extra) {
    // 1. Inherit, then apply the initial values of non-inherited properties.
    RefPtr<ComputedStyle> style = makeRef<ComputedStyle>();
    style->setValues(ComputedStyle::initial());
    style->inheritFrom(parentStyle);

    // 2. Collect the declarations that apply, sorted by cascade order.
    struct Candidate {
        const Declaration* declaration;
        uint64_t key;
    };
    std::vector<Candidate> candidates;

    std::vector<const StyleRule*> rules;
    index_.collect(element, rules);
    for (const StyleRule* rule : rules) {
        if (!matcher_.matches(element, rule->selector)) {
            continue;
        }
        for (const Declaration& declaration : *rule->declarations) {
            // key: origin/important layer (8) | specificity (32) | order (24)
            const uint64_t layer = declaration.important
                                       ? (rule->origin == Origin::UserAgent ? 5ull : 4ull)
                                       : (rule->origin == Origin::UserAgent ? 0ull : 1ull);
            const uint64_t key = (layer << 56) | (static_cast<uint64_t>(rule->selector.specificity) << 24) |
                                 (rule->order & 0xFFFFFFull);
            candidates.push_back(Candidate{&declaration, key});
        }
    }
    // Inline styles sit above author rules (and below !important author rules).
    if (const DeclarationBlock* inlineStyle = element.inlineStyle()) {
        for (const Declaration& declaration : *inlineStyle) {
            const uint64_t layer = declaration.important ? 4ull : 3ull;
            candidates.push_back(Candidate{&declaration, (layer << 56) | 0xFFFFFFFFFFull});
        }
    }

    // Keyframe declarations sit above everything else in the cascade, which is
    // what an animation means; they arrive already sorted among themselves.
    if (extra) {
        // Layer 3 is the inline layer; the maximum low part puts animations
        // above inline styles and still below anything !important (layer 4).
        for (const Declaration& declaration : *extra) {
            candidates.push_back(Candidate{&declaration, (3ull << 56) | 0xFFFFFFFFFFFFFFull});
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.key < b.key; });

    // 3. Apply in order; later declarations win.
    LengthContext context = lengthContext_;
    context.fontSize = parentStyle.fontSize; // em resolves against the parent until font-size is known

    // font-size first, because em/rem of every other property depends on it.
    for (const Candidate& candidate : candidates) {
        if (candidate.declaration->property != PropertyId::FontSize) {
            continue;
        }
        const CssValue& value = candidate.declaration->value;
        if (keywordIs(value, "inherit")) {
            style->fontSize = parentStyle.fontSize;
        } else if (keywordIs(value, "initial") || keywordIs(value, "unset") || keywordIs(value, "revert")) {
            style->fontSize = ComputedStyle::initial().fontSize;
        } else if (value.isLength()) {
            if (value.length.isPercent()) {
                style->fontSize = parentStyle.fontSize * value.length.value / 100.0f;
            } else {
                style->fontSize = resolveLength(value.length, context, style->fontSize);
            }
        }
    }
    style->fontSize = std::max(1.0f, style->fontSize);
    context.fontSize = style->fontSize;

    const auto resolvePx = [&](const Length& length, float fallback = 0.0f) {
        return resolveLength(length, context, fallback);
    };

    for (const Candidate& candidate : candidates) {
        const Declaration& declaration = *candidate.declaration;
        const PropertyId property = declaration.property;
        const CssValue& value = declaration.value;
        if (property == PropertyId::FontSize) {
            continue; // already applied
        }

        // Global keywords.
        if (value.isKeyword()) {
            const bool isInherit = keywordIs(value, "inherit");
            const bool isInitial = keywordIs(value, "initial") || keywordIs(value, "unset") ||
                                   keywordIs(value, "revert");
            if (isInherit || isInitial) {
                const ComputedStyle& source = isInherit ? parentStyle : ComputedStyle::initial();
                // Only the properties where this is actually useful.
                switch (property) {
                case PropertyId::Color:
                    style->color = source.color;
                    break;
                case PropertyId::Display:
                    style->display = isInherit ? source.display : ComputedStyle::initial().display;
                    break;
                case PropertyId::BackgroundColor:
                    style->backgroundColor = source.backgroundColor;
                    break;
                case PropertyId::FontWeight:
                    style->fontWeight = source.fontWeight;
                    break;
                case PropertyId::FontFamily:
                    style->fontFamily = source.fontFamily;
                    break;
                default:
                    break;
                }
                continue;
            }
        }

        switch (property) {
        case PropertyId::Display:
            style->display = toDisplay(value);
            break;
        case PropertyId::Position:
            style->position = toPosition(value);
            break;
        case PropertyId::Top:
            style->inset[kTop] = value.length;
            break;
        case PropertyId::Right:
            style->inset[kRight] = value.length;
            break;
        case PropertyId::Bottom:
            style->inset[kBottom] = value.length;
            break;
        case PropertyId::Left:
            style->inset[kLeft] = value.length;
            break;
        case PropertyId::ZIndex:
            if (keywordIs(value, "auto")) {
                style->zIndexAuto = true;
                style->zIndex = 0;
            } else {
                style->zIndexAuto = false;
                style->zIndex = static_cast<int>(value.length.value);
            }
            break;
        case PropertyId::Width:
            style->width = value.isKeyword() ? Length::automatic() : value.length;
            break;
        case PropertyId::Height:
            style->height = value.isKeyword() ? Length::automatic() : value.length;
            break;
        case PropertyId::MinWidth:
            style->minWidth = value.isKeyword() ? Length::automatic() : value.length;
            break;
        case PropertyId::MinHeight:
            style->minHeight = value.isKeyword() ? Length::automatic() : value.length;
            break;
        case PropertyId::MaxWidth:
            style->maxWidth = value.isKeyword() ? Length::none() : value.length;
            break;
        case PropertyId::MaxHeight:
            style->maxHeight = value.isKeyword() ? Length::none() : value.length;
            break;
        case PropertyId::MarginTop:
        case PropertyId::MarginRight:
        case PropertyId::MarginBottom:
        case PropertyId::MarginLeft: {
            const size_t side = static_cast<size_t>(property) - static_cast<size_t>(PropertyId::MarginTop);
            style->margin[side] = value.isKeyword() ? Length::automatic() : value.length;
            break;
        }
        case PropertyId::PaddingTop:
        case PropertyId::PaddingRight:
        case PropertyId::PaddingBottom:
        case PropertyId::PaddingLeft: {
            const size_t side = static_cast<size_t>(property) - static_cast<size_t>(PropertyId::PaddingTop);
            style->padding[side] = value.length;
            break;
        }
        case PropertyId::BorderTopWidth:
        case PropertyId::BorderRightWidth:
        case PropertyId::BorderBottomWidth:
        case PropertyId::BorderLeftWidth: {
            const size_t side = static_cast<size_t>(property) - static_cast<size_t>(PropertyId::BorderTopWidth);
            style->borderWidth[side] = std::max(0.0f, resolvePx(value.length));
            break;
        }
        case PropertyId::BorderTopStyle:
        case PropertyId::BorderRightStyle:
        case PropertyId::BorderBottomStyle:
        case PropertyId::BorderLeftStyle: {
            const size_t side = static_cast<size_t>(property) - static_cast<size_t>(PropertyId::BorderTopStyle);
            style->borderStyle[side] = toBorderStyle(value);
            break;
        }
        case PropertyId::BorderTopColor:
        case PropertyId::BorderRightColor:
        case PropertyId::BorderBottomColor:
        case PropertyId::BorderLeftColor: {
            const size_t side = static_cast<size_t>(property) - static_cast<size_t>(PropertyId::BorderTopColor);
            style->borderColor[side] = keywordIs(value, "currentcolor") ? style->color : value.color;
            break;
        }
        case PropertyId::BorderTopLeftRadius:
        case PropertyId::BorderTopRightRadius:
        case PropertyId::BorderBottomRightRadius:
        case PropertyId::BorderBottomLeftRadius: {
            const size_t corner =
                static_cast<size_t>(property) - static_cast<size_t>(PropertyId::BorderTopLeftRadius);
            style->borderRadius[corner] = value.length;
            break;
        }
        case PropertyId::BoxSizing:
            style->boxSizing = keywordIs(value, "border-box") ? BoxSizing::BorderBox : BoxSizing::ContentBox;
            break;
        case PropertyId::OverflowX:
            style->overflowX = toOverflow(value);
            break;
        case PropertyId::OverflowY:
            style->overflowY = toOverflow(value);
            break;

        case PropertyId::FlexDirection:
            style->flexDirection = toFlexDirection(value);
            break;
        case PropertyId::FlexWrap:
            style->flexWrap = keywordIs(value, "wrap")          ? FlexWrap::Wrap
                              : keywordIs(value, "wrap-reverse") ? FlexWrap::WrapReverse
                                                                 : FlexWrap::NoWrap;
            break;
        case PropertyId::JustifyContent:
            style->justifyContent = toJustify(value);
            break;
        case PropertyId::AlignItems:
            style->alignItems = toAlign(value);
            break;
        case PropertyId::AlignSelf:
            style->alignSelf = toAlign(value);
            break;
        case PropertyId::AlignContent:
            style->alignContent = toAlign(value);
            break;
        case PropertyId::FlexGrow:
            style->flexGrow = value.length.value;
            break;
        case PropertyId::FlexShrink:
            style->flexShrink = value.length.value;
            break;
        case PropertyId::FlexBasis:
            style->flexBasis = value.isKeyword() ? Length::automatic() : value.length;
            break;
        case PropertyId::RowGap:
            style->rowGap = value.length;
            break;
        case PropertyId::ColumnGap:
            style->columnGap = value.length;
            break;

        case PropertyId::BackgroundColor:
            style->backgroundColor = keywordIs(value, "currentcolor") ? style->color : value.color;
            break;
        case PropertyId::Transition:
            style->transitions = keywordIs(value, "none") ? std::vector<TransitionSpec>{} : toTransitions(value);
            break;
        case PropertyId::Animation:
            style->animation = keywordIs(value, "none") ? AnimationSpec{} : toAnimation(value);
            break;
        case PropertyId::BackgroundImage:
            style->backgroundImage = value.type == ValueType::Url ? value.text : std::string();
            style->backgroundGradient = value.type == ValueType::Function ? toGradient(value, *style) : Gradient{};
            break;
        case PropertyId::BackgroundRepeat:
            style->backgroundRepeat = toBackgroundRepeat(value);
            break;
        case PropertyId::BackgroundSize:
            if (keywordIs(value, "cover")) {
                style->backgroundSize = BackgroundSize{BackgroundSizeKind::Cover, Length::automatic(),
                                                       Length::automatic()};
            } else if (keywordIs(value, "contain")) {
                style->backgroundSize = BackgroundSize{BackgroundSizeKind::Contain, Length::automatic(),
                                                       Length::automatic()};
            } else if (value.type == ValueType::List && !value.items.empty()) {
                BackgroundSize size;
                size.kind = BackgroundSizeKind::Explicit;
                size.width = value.items[0].isLength() ? value.items[0].length : Length::automatic();
                size.height = value.items.size() > 1 && value.items[1].isLength() ? value.items[1].length
                                                                                 : Length::automatic();
                style->backgroundSize = size;
            } else if (value.isLength()) {
                style->backgroundSize = BackgroundSize{BackgroundSizeKind::Explicit, value.length,
                                                       Length::automatic()};
            }
            break;
        case PropertyId::BackgroundPosition:
            if (value.type == ValueType::List) {
                const auto axis = [&](size_t index, bool horizontal) -> Length {
                    if (index >= value.items.size()) {
                        return Length::percent(horizontal ? 0.0f : 50.0f);
                    }
                    const CssValue& item = value.items[index];
                    if (item.isLength()) {
                        return item.length;
                    }
                    if (keywordIs(item, "center")) {
                        return Length::percent(50.0f);
                    }
                    if (keywordIs(item, "right") || keywordIs(item, "bottom")) {
                        return Length::percent(100.0f);
                    }
                    return Length::percent(0.0f);
                };
                style->backgroundPosition[0] = axis(0, true);
                style->backgroundPosition[1] = axis(1, false);
            }
            break;
        case PropertyId::Color:
            style->color = value.isColor() ? value.color : style->color;
            break;
        case PropertyId::Opacity:
            style->opacity = std::clamp(value.length.value, 0.0f, 1.0f);
            break;
        case PropertyId::BoxShadow:
            style->boxShadow = keywordIs(value, "none") ? std::vector<BoxShadow>()
                                                        : toBoxShadows(value, context, style->color);
            break;
        case PropertyId::Visibility:
            style->visibility = keywordIs(value, "hidden")     ? Visibility::Hidden
                                : keywordIs(value, "collapse") ? Visibility::Collapse
                                                               : Visibility::Visible;
            break;
        case PropertyId::Transform:
            style->transform = keywordIs(value, "none") ? Transform() : toTransform(value, context);
            break;
        case PropertyId::TransformOrigin:
            if (value.type == ValueType::List) {
                const auto axis = [&](size_t index) -> Length {
                    if (index >= value.items.size()) {
                        return Length::percent(50.0f);
                    }
                    const CssValue& item = value.items[index];
                    if (item.isLength()) {
                        return item.length;
                    }
                    if (keywordIs(item, "left") || keywordIs(item, "top")) {
                        return Length::percent(0.0f);
                    }
                    if (keywordIs(item, "right") || keywordIs(item, "bottom")) {
                        return Length::percent(100.0f);
                    }
                    return Length::percent(50.0f);
                };
                style->transformOrigin[0] = axis(0);
                style->transformOrigin[1] = axis(1);
            }
            break;
        case PropertyId::PointerEvents:
            style->pointerEvents = keywordIs(value, "none") ? PointerEvents::None : PointerEvents::Auto;
            break;
        case PropertyId::Cursor:
            style->cursor = value.keyword;
            break;

        case PropertyId::FontFamily:
            style->fontFamily = toFontFamilies(value);
            break;
        case PropertyId::FontWeight:
            style->fontWeight = std::clamp(static_cast<int>(value.length.value), 1, 1000);
            break;
        case PropertyId::FontStyle:
            style->fontStyle = keywordIs(value, "italic")    ? FontStyle::Italic
                               : keywordIs(value, "oblique") ? FontStyle::Oblique
                                                             : FontStyle::Normal;
            break;
        case PropertyId::LineHeight:
            if (keywordIs(value, "normal")) {
                style->lineHeight = -1.0f;
            } else if (value.length.unit == LengthUnit::Number) {
                style->lineHeight = value.length.value * style->fontSize;
            } else if (value.length.isPercent()) {
                style->lineHeight = value.length.value / 100.0f * style->fontSize;
            } else {
                style->lineHeight = resolvePx(value.length, style->fontSize * 1.2f);
            }
            break;
        case PropertyId::LetterSpacing:
            style->letterSpacing = resolvePx(value.length);
            break;
        case PropertyId::TextAlign:
            style->textAlign = toTextAlign(value);
            break;
        case PropertyId::TextDecorationLine: {
            uint8_t lines = kDecorationNone;
            const auto apply = [&](const CssValue& item) {
                if (keywordIs(item, "underline")) lines |= kDecorationUnderline;
                if (keywordIs(item, "overline")) lines |= kDecorationOverline;
                if (keywordIs(item, "line-through")) lines |= kDecorationLineThrough;
            };
            if (value.type == ValueType::List) {
                for (const CssValue& item : value.items) {
                    apply(item);
                }
            } else {
                apply(value);
            }
            style->textDecorationLine = lines;
            break;
        }
        case PropertyId::TextDecorationColor:
            style->textDecorationColor = keywordIs(value, "currentcolor") ? style->color : value.color;
            break;
        case PropertyId::TextTransform:
            style->textTransform = keywordIs(value, "uppercase")    ? TextTransform::Uppercase
                                   : keywordIs(value, "lowercase")  ? TextTransform::Lowercase
                                   : keywordIs(value, "capitalize") ? TextTransform::Capitalize
                                                                    : TextTransform::None;
            break;
        case PropertyId::WhiteSpace:
            style->whiteSpace = toWhiteSpace(value);
            break;
        case PropertyId::TextOverflow:
            style->textOverflow = keywordIs(value, "ellipsis") ? TextOverflow::Ellipsis : TextOverflow::Clip;
            break;

        case PropertyId::FontSize:
        case PropertyId::Invalid:
        case PropertyId::Count:
            break;
        }
    }

    // 4. Used values that depend on other properties.
    for (size_t side = 0; side < 4; ++side) {
        if (style->borderStyle[side] == BorderStyle::None || style->borderStyle[side] == BorderStyle::Hidden) {
            style->borderWidth[side] = 0.0f;
        }
    }
    if (style->textDecorationColor == ComputedStyle::initial().textDecorationColor &&
        style->textDecorationLine != kDecorationNone) {
        style->textDecorationColor = style->color;
    }
    // Absolutely positioned and floated boxes are blockified.
    if (style->position == PositionType::Absolute || style->position == PositionType::Fixed) {
        if (style->display == Display::Inline || style->display == Display::InlineBlock) {
            style->display = Display::Block;
        } else if (style->display == Display::InlineFlex) {
            style->display = Display::Flex;
        }
    }
    return style;
}

void StyleEngine::recalcSubtree(dom::Element& element, const ComputedStyle& parentStyle, bool force) {
    const bool selfDirty = force || (element.dirtyBits() & dom::kDirtyStyleSelf) != 0;
    const bool childrenDirty = force || (element.dirtyBits() & dom::kDirtyStyleChildren) != 0;
    if (!selfDirty && !childrenDirty && !animator_.hasRunning()) {
        return;
    }

    // An element with something running restyles every frame, even when nothing
    // else touched it.
    const bool animating = animator_.hasRunning() && animator_.isAnimating(element);

    bool inheritedChanged = force;
    if (selfDirty || animating || !element.computedStyle()) {
        RefPtr<ComputedStyle> computed = computeStyle(element, parentStyle);
        // Transitions and animations turn the cascade's result into what is
        // actually shown this frame.
        computed = animator_.apply(element, std::move(computed),
                                   [this, &element, &parentStyle](const DeclarationBlock* extra) {
                                       return computeStyle(element, parentStyle, extra);
                                   });
        const ComputedStyle* previous = element.computedStyle();
        if (previous) {
            const ComputedStyle::Diff difference = ComputedStyle::diff(*previous, *computed);
            if (difference.layout) {
                element.markDirty(dom::kDirtyLayout | dom::kDirtyLayoutTree);
            }
            if (difference.paint) {
                element.markDirty(dom::kDirtyPaintSelf);
            }
            inheritedChanged = inheritedChanged || difference.inheritedChanged;
        } else {
            element.markDirty(dom::kDirtyLayout | dom::kDirtyLayoutTree | dom::kDirtyPaintSelf);
            inheritedChanged = true;
        }
        element.setComputedStyle(std::move(computed));
    }

    const ComputedStyle& style = *element.computedStyle();
    for (dom::Element* child : element.childElements()) {
        recalcSubtree(*child, style, force || inheritedChanged);
    }
    element.clearDirty(dom::kDirtyStyleSelf | dom::kDirtyStyleChildren);
}

void StyleEngine::recalcStyles(float viewportWidth, float viewportHeight, double timeSeconds) {
    animator_.setTime(timeSeconds);
    if (indexDirty_) {
        rebuildIndex();
    }
    matcher_ = SelectorMatcher(document_.elementStateProvider());

    dom::Element* root = document_.documentElement();
    if (!root) {
        return;
    }

    lengthContext_.viewportWidth = viewportWidth;
    lengthContext_.viewportHeight = viewportHeight;
    lengthContext_.rootFontSize = ComputedStyle::initial().fontSize;
    lengthContext_.fontSize = lengthContext_.rootFontSize;

    // The root's own style first, so rem resolves against it.
    const bool force = forceFullRecalc_;
    if (force || (root->dirtyBits() & dom::kDirtyStyleSelf) != 0 || !root->computedStyle()) {
        RefPtr<ComputedStyle> rootStyle = computeStyle(*root, ComputedStyle::initial());
        root->setComputedStyle(rootStyle);
    }
    lengthContext_.rootFontSize = root->computedStyle()->fontSize;

    recalcSubtree(*root, ComputedStyle::initial(), force);
    document_.clearDirty(dom::kDirtyStyleSelf | dom::kDirtyStyleChildren);
    forceFullRecalc_ = false;
}

const ComputedStyle* StyleEngine::styleFor(const dom::Element& element) const { return element.computedStyle(); }

} // namespace xgu::css
