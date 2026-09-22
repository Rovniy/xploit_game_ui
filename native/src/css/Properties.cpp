#include "css/Properties.h"

#include <array>
#include <cctype>
#include <string>
#include <unordered_map>

namespace xgu::css {
namespace {

// Keep in sync with PropertyId; the entry index is the enum value.
constexpr std::array<PropertyMeta, kPropertyCount> kProperties = {{
    {"", false, false}, // Invalid

    {"display", false, true},
    {"position", false, true},
    {"top", false, true},
    {"right", false, true},
    {"bottom", false, true},
    {"left", false, true},
    {"z-index", false, false},
    {"width", false, true},
    {"height", false, true},
    {"min-width", false, true},
    {"min-height", false, true},
    {"max-width", false, true},
    {"max-height", false, true},
    {"margin-top", false, true},
    {"margin-right", false, true},
    {"margin-bottom", false, true},
    {"margin-left", false, true},
    {"padding-top", false, true},
    {"padding-right", false, true},
    {"padding-bottom", false, true},
    {"padding-left", false, true},
    {"border-top-width", false, true},
    {"border-right-width", false, true},
    {"border-bottom-width", false, true},
    {"border-left-width", false, true},
    {"border-top-style", false, true},
    {"border-right-style", false, true},
    {"border-bottom-style", false, true},
    {"border-left-style", false, true},
    {"border-top-color", false, false},
    {"border-right-color", false, false},
    {"border-bottom-color", false, false},
    {"border-left-color", false, false},
    {"border-top-left-radius", false, false},
    {"border-top-right-radius", false, false},
    {"border-bottom-right-radius", false, false},
    {"border-bottom-left-radius", false, false},
    {"box-sizing", false, true},
    {"overflow-x", false, true},
    {"overflow-y", false, true},

    {"flex-direction", false, true},
    {"flex-wrap", false, true},
    {"justify-content", false, true},
    {"align-items", false, true},
    {"align-self", false, true},
    {"align-content", false, true},
    {"flex-grow", false, true},
    {"flex-shrink", false, true},
    {"flex-basis", false, true},
    {"row-gap", false, true},
    {"column-gap", false, true},

    {"background-color", false, false},
    {"background-image", false, false},
    {"background-size", false, false},
    {"background-position", false, false},
    {"background-repeat", false, false},
    {"color", true, false},
    {"opacity", false, false},
    {"box-shadow", false, false},
    {"visibility", true, false},
    {"transform", false, false},
    {"transform-origin", false, false},
    {"pointer-events", true, false},
    {"cursor", true, false},

    {"font-family", true, true},
    {"font-size", true, true},
    {"font-weight", true, true},
    {"font-style", true, true},
    {"line-height", true, true},
    {"letter-spacing", true, true},
    {"text-align", true, true},
    {"text-decoration-line", false, false},
    {"text-decoration-color", false, false},
    {"text-transform", true, true},
    {"white-space", true, true},
    {"text-overflow", false, true},

    {"transition", false, false},
    {"animation", false, false},
}};

const std::unordered_map<std::string_view, PropertyId>& nameMap() {
    static const std::unordered_map<std::string_view, PropertyId>* map = [] {
        auto* result = new std::unordered_map<std::string_view, PropertyId>();
        for (size_t i = 1; i < kProperties.size(); ++i) {
            result->emplace(kProperties[i].name, static_cast<PropertyId>(i));
        }
        return result;
    }();
    return *map;
}

} // namespace

const PropertyMeta& propertyMeta(PropertyId id) {
    const size_t index = static_cast<size_t>(id);
    return kProperties[index < kProperties.size() ? index : 0];
}

PropertyId propertyFromName(std::string_view name) {
    if (name.empty() || name.size() > 32) {
        return PropertyId::Invalid;
    }
    char lowered[32];
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        lowered[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    const auto& map = nameMap();
    const auto it = map.find(std::string_view(lowered, name.size()));
    return it == map.end() ? PropertyId::Invalid : it->second;
}

std::string_view propertyName(PropertyId id) { return propertyMeta(id).name; }

bool propertyIsInherited(PropertyId id) { return propertyMeta(id).inherited; }

bool propertyAffectsLayout(PropertyId id) { return propertyMeta(id).affectsLayout; }

} // namespace xgu::css
