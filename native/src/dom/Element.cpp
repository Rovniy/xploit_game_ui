#include "dom/Element.h"

#include "dom/Document.h"
#include "dom/Serializer.h"
#include "html/LexborHtmlParser.h"

#include <algorithm>
#include <cctype>

namespace xgu::dom {
namespace {

const Atom& idAttribute() {
    static const Atom atom("id");
    return atom;
}

const Atom& classAttribute() {
    static const Atom atom("class");
    return atom;
}

bool isHtmlSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::vector<Atom> splitClasses(std::string_view value) {
    std::vector<Atom> result;
    size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && isHtmlSpace(value[i])) {
            ++i;
        }
        const size_t start = i;
        while (i < value.size() && !isHtmlSpace(value[i])) {
            ++i;
        }
        if (i > start) {
            Atom name(value.substr(start, i - start));
            if (std::find(result.begin(), result.end(), name) == result.end()) {
                result.push_back(name);
            }
        }
    }
    return result;
}

} // namespace

Element::Element(Document* document, Atom tagName)
    : Node(NodeType::Element, document), tagName_(tagName), knownTag_(html::tagFromAtom(tagName)) {}

std::string Element::nodeName() const {
    std::string upper = tagName_.string();
    for (char& c : upper) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return upper;
}

bool Element::hasClass(const Atom& name) const {
    return std::find(classes_.begin(), classes_.end(), name) != classes_.end();
}

std::string Element::className() const { return getAttributeOrEmpty(classAttribute()); }

void Element::setClassName(std::string_view value) { setAttribute(classAttribute(), value); }

bool Element::addClass(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    const Atom atom(name);
    if (hasClass(atom)) {
        return false;
    }
    std::string value = className();
    if (!value.empty()) {
        value.push_back(' ');
    }
    value.append(name);
    setClassName(value);
    return true;
}

bool Element::removeClass(std::string_view name) {
    const Atom atom(name);
    if (!hasClass(atom)) {
        return false;
    }
    std::string value;
    for (const Atom& existing : classes_) {
        if (existing == atom) {
            continue;
        }
        if (!value.empty()) {
            value.push_back(' ');
        }
        value.append(existing.string());
    }
    setClassName(value);
    return true;
}

bool Element::toggleClass(std::string_view name) {
    if (hasClass(Atom(name))) {
        removeClass(name);
        return false;
    }
    addClass(name);
    return true;
}

bool Element::hasAttribute(const Atom& name) const { return getAttribute(name) != nullptr; }

const std::string* Element::getAttribute(const Atom& name) const {
    for (const Attribute& attribute : attributes_) {
        if (attribute.name == name) {
            return &attribute.value;
        }
    }
    return nullptr;
}

std::string Element::getAttributeOrEmpty(const Atom& name) const {
    const std::string* value = getAttribute(name);
    return value ? *value : std::string();
}

void Element::setAttribute(const Atom& name, std::string_view value) {
    bool changed = true;
    bool found = false;
    for (Attribute& attribute : attributes_) {
        if (attribute.name == name) {
            changed = attribute.value != value;
            attribute.value.assign(value);
            found = true;
            break;
        }
    }
    if (!found) {
        attributes_.push_back(Attribute{name, std::string(value)});
    }
    if (!changed) {
        return;
    }
    if (name == idAttribute()) {
        updateIdFromAttribute(value);
    } else if (name == classAttribute()) {
        updateClassesFromAttribute(value);
    }
    markDirty(kDirtyStyleSelf | kDirtyStyleChildren);
    if (Document* doc = document()) {
        doc->notifyAttributeChanged(*this, name);
    }
}

bool Element::removeAttribute(const Atom& name) {
    const auto it = std::find_if(attributes_.begin(), attributes_.end(),
                                 [&](const Attribute& attribute) { return attribute.name == name; });
    if (it == attributes_.end()) {
        return false;
    }
    attributes_.erase(it);
    if (name == idAttribute()) {
        updateIdFromAttribute({});
    } else if (name == classAttribute()) {
        updateClassesFromAttribute({});
    }
    markDirty(kDirtyStyleSelf | kDirtyStyleChildren);
    if (Document* doc = document()) {
        doc->notifyAttributeChanged(*this, name);
    }
    return true;
}

void Element::updateIdFromAttribute(std::string_view value) {
    const Atom newId = value.empty() ? Atom() : Atom(value);
    if (newId == id_) {
        return;
    }
    Document* doc = isConnected() ? document() : nullptr;
    if (doc && !id_.empty()) {
        doc->unregisterId(id_, *this);
    }
    id_ = newId;
    if (doc && !id_.empty()) {
        doc->registerId(id_, *this);
    }
}

void Element::updateClassesFromAttribute(std::string_view value) { classes_ = splitClasses(value); }

void Element::didConnect() {
    if (!id_.empty()) {
        if (Document* doc = document()) {
            doc->registerId(id_, *this);
        }
    }
}

void Element::didDisconnect() {
    if (!id_.empty()) {
        if (Document* doc = document()) {
            doc->unregisterId(id_, *this);
        }
    }
}

std::vector<Element*> Element::childElements() const {
    std::vector<Element*> result;
    for (size_t i = 0; i < childCount(); ++i) {
        Node* child = childAt(i);
        if (child->isElement()) {
            result.push_back(static_cast<Element*>(child));
        }
    }
    return result;
}

std::string Element::innerHTML() const { return serializeChildren(*this); }

std::string Element::outerHTML() const { return serializeNode(*this); }

bool Element::setInnerHTML(std::string_view htmlText) {
    Document* doc = document();
    if (!doc) {
        return false;
    }
    html::LexborHtmlParser parser;
    RefPtr<DocumentFragment> fragment = parser.parseFragment(htmlText, *this, *doc);
    if (!fragment) {
        return false;
    }
    removeAllChildren();
    while (Node* child = fragment->firstChild()) {
        appendChild(*child);
    }
    return true;
}

} // namespace xgu::dom
