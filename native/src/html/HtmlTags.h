#pragma once

#include "core/Atom.h"

#include <cstdint>
#include <string_view>

namespace xgu::html {

// Tags the engine treats specially. Everything else parses and lives in the DOM
// as an unknown element with default (inline) styling.
enum class HtmlTag : uint8_t {
    Unknown = 0,
    Html,
    Head,
    Body,
    Title,
    Meta,
    Link,
    Style,
    Script,
    Div,
    Span,
    P,
    Img,
    Button,
    Input,
    Textarea,
    Label,
    Ul,
    Li,
    H1,
    H2,
    H3,
    H4,
    H5,
    H6,
    Br,
    A,
    Strong,
    Em,
    Count,
};

// Case-insensitive lookup; returns HtmlTag::Unknown for anything else.
HtmlTag tagFromName(std::string_view name);
HtmlTag tagFromAtom(const Atom& name);

// Canonical lowercase name, or "" for Unknown.
std::string_view tagName(HtmlTag tag);

// Void elements never have children and serialise without a closing tag.
bool isVoidTag(HtmlTag tag);

// Content of these elements is raw text (not parsed as markup) when serialising.
bool isRawTextTag(HtmlTag tag);

// Elements that never render (their subtree produces no boxes).
bool isNonRenderedTag(HtmlTag tag);

} // namespace xgu::html
