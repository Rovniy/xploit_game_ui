#include "css/SelectorMatcher.h"

#include "dom/Document.h"
#include "dom/Element.h"

#include <algorithm>
#include <cctype>

namespace xgu::css {
namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equals(std::string_view a, std::string_view b, bool caseInsensitive) {
    if (a.size() != b.size()) {
        return false;
    }
    if (!caseInsensitive) {
        return a == b;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

bool contains(std::string_view haystack, std::string_view needle, bool caseInsensitive) {
    if (needle.empty()) {
        return false;
    }
    if (!caseInsensitive) {
        return haystack.find(needle) != std::string_view::npos;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        if (equals(haystack.substr(i, needle.size()), needle, true)) {
            return true;
        }
    }
    return false;
}

bool matchesAttribute(const dom::Element& element, const AttributeSelector& selector) {
    const std::string* value = element.getAttribute(selector.name);
    if (!value) {
        return false;
    }
    const std::string_view actual = *value;
    const std::string_view expected = selector.value;
    switch (selector.match) {
    case AttributeMatch::Present:
        return true;
    case AttributeMatch::Exact:
        return equals(actual, expected, selector.caseInsensitive);
    case AttributeMatch::Includes: {
        if (expected.empty()) {
            return false;
        }
        size_t start = 0;
        while (start <= actual.size()) {
            size_t end = actual.find_first_of(" \t\n\r\f", start);
            if (end == std::string_view::npos) {
                end = actual.size();
            }
            if (end > start && equals(actual.substr(start, end - start), expected, selector.caseInsensitive)) {
                return true;
            }
            start = end + 1;
        }
        return false;
    }
    case AttributeMatch::DashMatch:
        if (equals(actual, expected, selector.caseInsensitive)) {
            return true;
        }
        return actual.size() > expected.size() && actual[expected.size()] == '-' &&
               equals(actual.substr(0, expected.size()), expected, selector.caseInsensitive);
    case AttributeMatch::Prefix:
        return !expected.empty() && actual.size() >= expected.size() &&
               equals(actual.substr(0, expected.size()), expected, selector.caseInsensitive);
    case AttributeMatch::Suffix:
        return !expected.empty() && actual.size() >= expected.size() &&
               equals(actual.substr(actual.size() - expected.size()), expected, selector.caseInsensitive);
    case AttributeMatch::Substring:
        return contains(actual, expected, selector.caseInsensitive);
    }
    return false;
}

bool hasElementChildOrText(const dom::Element& element) {
    for (size_t i = 0; i < element.childCount(); ++i) {
        const dom::Node* child = element.childAt(i);
        if (child->isElement()) {
            return true;
        }
        if (child->isText() && !static_cast<const dom::CharacterData*>(child)->data().empty()) {
            return true;
        }
    }
    return false;
}

const Atom& disabledAttribute() {
    static const Atom atom("disabled");
    return atom;
}

const Atom& checkedAttribute() {
    static const Atom atom("checked");
    return atom;
}

} // namespace

bool SelectorMatcher::matchesPseudo(const dom::Element& element, const PseudoSelector& pseudo) const {
    switch (pseudo.kind) {
    case PseudoClass::Hover:
        return state_ && state_->isHovered(element);
    case PseudoClass::Active:
        return state_ && state_->isActive(element);
    case PseudoClass::Focus:
        return state_ && state_->isFocused(element);
    case PseudoClass::FocusWithin:
        return state_ && state_->isFocusWithin(element);
    case PseudoClass::Disabled:
        return element.hasAttribute(disabledAttribute());
    case PseudoClass::Enabled:
        return !element.hasAttribute(disabledAttribute());
    case PseudoClass::Checked:
        return element.hasAttribute(checkedAttribute());
    case PseudoClass::FirstChild: {
        const dom::Element* parent = element.parentElement();
        return parent && parent->childElements().front() == &element;
    }
    case PseudoClass::LastChild: {
        const dom::Element* parent = element.parentElement();
        return parent && parent->childElements().back() == &element;
    }
    case PseudoClass::OnlyChild: {
        const dom::Element* parent = element.parentElement();
        return parent && parent->childElements().size() == 1;
    }
    case PseudoClass::Empty:
        return !hasElementChildOrText(element);
    case PseudoClass::Root:
        return element.document() && element.document()->documentElement() == &element;
    case PseudoClass::Not:
        for (const Selector& inner : pseudo.arguments) {
            if (matches(element, inner)) {
                return false;
            }
        }
        return true;
    case PseudoClass::Unknown:
        break;
    }
    return false;
}

bool SelectorMatcher::matchesCompound(const dom::Element& element, const CompoundSelector& compound) const {
    if (!compound.tagName.empty() && element.tagName() != compound.tagName) {
        return false;
    }
    if (!compound.id.empty() && element.id() != compound.id) {
        return false;
    }
    for (const Atom& className : compound.classes) {
        if (!element.hasClass(className)) {
            return false;
        }
    }
    for (const AttributeSelector& attribute : compound.attributes) {
        if (!matchesAttribute(element, attribute)) {
            return false;
        }
    }
    for (const PseudoSelector& pseudo : compound.pseudos) {
        if (!matchesPseudo(element, pseudo)) {
            return false;
        }
    }
    return true;
}

bool SelectorMatcher::matchesFrom(const dom::Element& element, const Selector& selector, size_t compoundIndex) const {
    if (!matchesCompound(element, selector.compounds[compoundIndex])) {
        return false;
    }
    if (compoundIndex == 0) {
        return true;
    }
    const Combinator combinator = selector.compounds[compoundIndex].combinatorToLeft;
    const size_t next = compoundIndex - 1;
    switch (combinator) {
    case Combinator::Descendant:
        for (dom::Element* ancestor = element.parentElement(); ancestor; ancestor = ancestor->parentElement()) {
            if (matchesFrom(*ancestor, selector, next)) {
                return true;
            }
        }
        return false;
    case Combinator::Child: {
        dom::Element* parent = element.parentElement();
        return parent && matchesFrom(*parent, selector, next);
    }
    case Combinator::NextSibling: {
        // Previous element sibling.
        for (dom::Node* sibling = element.previousSibling(); sibling; sibling = sibling->previousSibling()) {
            if (sibling->isElement()) {
                return matchesFrom(static_cast<dom::Element&>(*sibling), selector, next);
            }
        }
        return false;
    }
    case Combinator::SubsequentSibling:
        for (dom::Node* sibling = element.previousSibling(); sibling; sibling = sibling->previousSibling()) {
            if (sibling->isElement() && matchesFrom(static_cast<dom::Element&>(*sibling), selector, next)) {
                return true;
            }
        }
        return false;
    case Combinator::None:
        return true;
    }
    return false;
}

bool SelectorMatcher::matches(const dom::Element& element, const Selector& selector) const {
    if (selector.empty()) {
        return false;
    }
    return matchesFrom(element, selector, selector.compounds.size() - 1);
}

bool SelectorMatcher::matches(const dom::Element& element, const SelectorList& list) const {
    for (const Selector& selector : list.selectors) {
        if (matches(element, selector)) {
            return true;
        }
    }
    return false;
}

dom::Element* SelectorMatcher::queryFirst(dom::Node& root, const SelectorList& list) const {
    for (dom::Node* node = dom::nextInTreeOrder(&root, &root); node; node = dom::nextInTreeOrder(node, &root)) {
        if (node->isElement() && matches(static_cast<dom::Element&>(*node), list)) {
            return static_cast<dom::Element*>(node);
        }
    }
    return nullptr;
}

std::vector<dom::Element*> SelectorMatcher::queryAll(dom::Node& root, const SelectorList& list) const {
    std::vector<dom::Element*> result;
    for (dom::Node* node = dom::nextInTreeOrder(&root, &root); node; node = dom::nextInTreeOrder(node, &root)) {
        if (node->isElement() && matches(static_cast<dom::Element&>(*node), list)) {
            result.push_back(static_cast<dom::Element*>(node));
        }
    }
    return result;
}

} // namespace xgu::css
