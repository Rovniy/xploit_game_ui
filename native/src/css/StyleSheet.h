#pragma once

#include "css/CssValue.h"
#include "css/Properties.h"
#include "css/Selector.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace xgu::css {

// Where a declaration came from; decides who wins in the cascade.
enum class Origin : uint8_t { UserAgent = 0, Author = 1, Inline = 2 };

struct Declaration {
    PropertyId property = PropertyId::Invalid;
    CssValue value;
    bool important = false;
};

// Declarations of one rule (or of a style="" attribute), in source order.
using DeclarationBlock = std::vector<Declaration>;

struct StyleRule {
    Selector selector;
    // Shared between the selectors of one rule ("a, b { ... }").
    std::shared_ptr<const DeclarationBlock> declarations;
    uint32_t order = 0; // position in the stylesheet, for the cascade tiebreak
    Origin origin = Origin::Author;
};

// A parsed stylesheet: one flattened rule per selector.
struct StyleSheet {
    std::vector<StyleRule> rules;
    // Diagnostics collected while parsing (unknown properties, bad values).
    std::vector<std::string> warnings;

    bool empty() const { return rules.empty(); }
};

// Parses a stylesheet. `origin` and `firstOrder` position its rules in the
// cascade; `firstOrder` should continue the numbering of earlier sheets.
StyleSheet parseStyleSheet(std::string_view css, Origin origin = Origin::Author, uint32_t firstOrder = 0);

// Parses the contents of a style="" attribute (no selectors, no braces).
DeclarationBlock parseDeclarationBlock(std::string_view text, std::vector<std::string>* warnings = nullptr);

// Parses one "name: value" pair, expanding shorthands. Returns false when the
// property is unknown or the value cannot be parsed.
bool parseDeclaration(std::string_view name, std::string_view value, DeclarationBlock& out,
                      std::vector<std::string>* warnings = nullptr);

// Serialises a declaration block back to CSS text (style attribute round trip).
std::string serializeDeclarations(const DeclarationBlock& block);
// Serialises a single value the way getComputedStyle/style.getPropertyValue do.
std::string serializeValue(const CssValue& value);

} // namespace xgu::css
