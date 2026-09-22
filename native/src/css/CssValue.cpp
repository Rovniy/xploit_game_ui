#include "css/CssValue.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_map>

namespace xgu::css {
namespace {

struct NamedColorEntry {
    std::string_view name;
    Color color;
};

// The colours a game UI actually uses, plus the CSS basic set. Anything else is
// a parse error (authors use hex or rgb() for the rest).
constexpr std::array<NamedColorEntry, 34> kNamedColors = {{
    {"transparent", Color{0, 0, 0, 0}},
    {"black", Color{0, 0, 0, 255}},
    {"white", Color{255, 255, 255, 255}},
    {"red", Color{255, 0, 0, 255}},
    {"lime", Color{0, 255, 0, 255}},
    {"green", Color{0, 128, 0, 255}},
    {"blue", Color{0, 0, 255, 255}},
    {"yellow", Color{255, 255, 0, 255}},
    {"cyan", Color{0, 255, 255, 255}},
    {"aqua", Color{0, 255, 255, 255}},
    {"magenta", Color{255, 0, 255, 255}},
    {"fuchsia", Color{255, 0, 255, 255}},
    {"silver", Color{192, 192, 192, 255}},
    {"gray", Color{128, 128, 128, 255}},
    {"grey", Color{128, 128, 128, 255}},
    {"darkgray", Color{169, 169, 169, 255}},
    {"darkgrey", Color{169, 169, 169, 255}},
    {"lightgray", Color{211, 211, 211, 255}},
    {"lightgrey", Color{211, 211, 211, 255}},
    {"maroon", Color{128, 0, 0, 255}},
    {"olive", Color{128, 128, 0, 255}},
    {"navy", Color{0, 0, 128, 255}},
    {"purple", Color{128, 0, 128, 255}},
    {"teal", Color{0, 128, 128, 255}},
    {"orange", Color{255, 165, 0, 255}},
    {"gold", Color{255, 215, 0, 255}},
    {"pink", Color{255, 192, 203, 255}},
    {"brown", Color{165, 42, 42, 255}},
    {"crimson", Color{220, 20, 60, 255}},
    {"tomato", Color{255, 99, 71, 255}},
    {"steelblue", Color{70, 130, 180, 255}},
    {"skyblue", Color{135, 206, 235, 255}},
    {"limegreen", Color{50, 205, 50, 255}},
    {"dodgerblue", Color{30, 144, 255, 255}},
}};

} // namespace

bool namedColor(std::string_view name, Color& out) {
    if (name.empty() || name.size() > 20) {
        return false;
    }
    std::string lowered(name);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const NamedColorEntry& entry : kNamedColors) {
        if (entry.name == lowered) {
            out = entry.color;
            return true;
        }
    }
    return false;
}

float resolveLength(const Length& length, const LengthContext& context, float fallback) {
    switch (length.unit) {
    case LengthUnit::Px:
    case LengthUnit::Number:
        return length.value;
    case LengthUnit::Em:
        return length.value * context.fontSize;
    case LengthUnit::Rem:
        return length.value * context.rootFontSize;
    case LengthUnit::Vw:
        return length.value * context.viewportWidth / 100.0f;
    case LengthUnit::Vh:
        return length.value * context.viewportHeight / 100.0f;
    case LengthUnit::Vmin:
        return length.value * std::min(context.viewportWidth, context.viewportHeight) / 100.0f;
    case LengthUnit::Vmax:
        return length.value * std::max(context.viewportWidth, context.viewportHeight) / 100.0f;
    case LengthUnit::Percent:
    case LengthUnit::Auto:
    case LengthUnit::None:
    case LengthUnit::Seconds:
        break;
    }
    return fallback;
}

} // namespace xgu::css
