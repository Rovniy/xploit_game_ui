#pragma once

#include "core/Atom.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xgu::css {

enum class Combinator : uint8_t {
    None,       // leftmost compound
    Descendant, // "a b"
    Child,      // "a > b"
    NextSibling,     // "a + b"
    SubsequentSibling, // "a ~ b"
};

enum class AttributeMatch : uint8_t {
    Present,    // [attr]
    Exact,      // [attr=value]
    Includes,   // [attr~=value]
    DashMatch,  // [attr|=value]
    Prefix,     // [attr^=value]
    Suffix,     // [attr$=value]
    Substring,  // [attr*=value]
};

struct AttributeSelector {
    Atom name;
    AttributeMatch match = AttributeMatch::Present;
    std::string value;
    bool caseInsensitive = false;
};

enum class PseudoClass : uint8_t {
    Unknown,
    Hover,
    Active,
    Focus,
    FocusWithin,
    Disabled,
    Enabled,
    Checked,
    FirstChild,
    LastChild,
    OnlyChild,
    Empty,
    Root,
    Not, // holds a nested selector list
};

struct Selector;

struct PseudoSelector {
    PseudoClass kind = PseudoClass::Unknown;
    // :not(...) argument; empty otherwise.
    std::vector<Selector> arguments;
};

// One compound selector: "div#id.a.b[attr]:hover".
struct CompoundSelector {
    Atom tagName;              // empty = any
    bool universal = false;    // "*"
    Atom id;                   // empty = none
    std::vector<Atom> classes;
    std::vector<AttributeSelector> attributes;
    std::vector<PseudoSelector> pseudos;
    // How this compound joins the one to its left.
    Combinator combinatorToLeft = Combinator::None;
};

// A complex selector, stored left to right; the rightmost compound is last.
struct Selector {
    std::vector<CompoundSelector> compounds;
    uint32_t specificity = 0;

    const CompoundSelector& rightmost() const { return compounds.back(); }
    bool empty() const { return compounds.empty(); }
};

// A comma-separated list ("a, b > c").
struct SelectorList {
    std::vector<Selector> selectors;

    bool empty() const { return selectors.empty(); }
    // Returns nullopt when the text is not a selector the engine understands;
    // `error` then explains why.
    static std::optional<SelectorList> parse(std::string_view text, std::string* error = nullptr);
};

// CSS specificity packed as (ids << 20) | (classes << 10) | types, saturating.
uint32_t computeSpecificity(const Selector& selector);

} // namespace xgu::css
