#pragma once

#include "core/RefCounted.h"

#include <string_view>

namespace xgu::dom {
class Document;
class DocumentFragment;
class Element;
} // namespace xgu::dom

namespace xgu::html {

// Parses HTML with lexbor and converts the result into the engine's own DOM.
// The lexbor tree is destroyed before returning, so nothing outside this class
// depends on lexbor types (see ADR-0001: lexbor is the parser, not the DOM).
//
// Runtime thread only; each call creates and destroys its own lexbor document.
class LexborHtmlParser {
public:
    // Replaces the document's children with the parsed tree and refreshes
    // documentElement/head/body. Returns false on a parser error.
    bool parseDocument(std::string_view html, dom::Document& document);

    // Parses a fragment in the context of `contextElement` (innerHTML). The
    // returned fragment's children are ready to be moved into the tree.
    RefPtr<dom::DocumentFragment> parseFragment(std::string_view html, dom::Element& contextElement,
                                                dom::Document& document);
};

} // namespace xgu::html
