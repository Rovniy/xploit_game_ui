#pragma once

#include "css/Selector.h"

#include <vector>

namespace xgu::dom {
class Element;
class Node;
} // namespace xgu::dom

namespace xgu::css {

// Element state the pseudo-classes test. The input router owns these flags from
// Stage 6; until then they are all false.
struct ElementStateProvider {
    virtual ~ElementStateProvider() = default;
    virtual bool isHovered(const dom::Element&) const { return false; }
    virtual bool isActive(const dom::Element&) const { return false; }
    virtual bool isFocused(const dom::Element&) const { return false; }
    virtual bool isFocusWithin(const dom::Element&) const { return false; }
};

class SelectorMatcher {
public:
    explicit SelectorMatcher(const ElementStateProvider* state = nullptr) : state_(state) {}

    bool matches(const dom::Element& element, const Selector& selector) const;
    bool matches(const dom::Element& element, const SelectorList& list) const;

    // Elements of `root`'s subtree (excluding `root` itself for querySelector
    // semantics on a document, including descendants only) in tree order.
    dom::Element* queryFirst(dom::Node& root, const SelectorList& list) const;
    std::vector<dom::Element*> queryAll(dom::Node& root, const SelectorList& list) const;

private:
    bool matchesCompound(const dom::Element& element, const CompoundSelector& compound) const;
    bool matchesPseudo(const dom::Element& element, const PseudoSelector& pseudo) const;
    // Walks the complex selector right to left starting at `compoundIndex`.
    bool matchesFrom(const dom::Element& element, const Selector& selector, size_t compoundIndex) const;

    const ElementStateProvider* state_ = nullptr;
};

} // namespace xgu::css
