#include "dom/Document.h"

#include <algorithm>

namespace xgu::dom {

Document::Document() : Node(NodeType::Document, nullptr) {}

Document::~Document() {
    // Children are released by ~Node; drop the raw pointers first.
    documentElement_ = nullptr;
    head_ = nullptr;
    body_ = nullptr;
    idMap_.clear();
}

RefPtr<Element> Document::createElement(std::string_view tagName) {
    return makeRef<Element>(this, Atom::lowered(tagName));
}

RefPtr<Text> Document::createTextNode(std::string_view data) { return makeRef<Text>(this, data); }

RefPtr<Comment> Document::createComment(std::string_view data) { return makeRef<Comment>(this, data); }

RefPtr<DocumentFragment> Document::createDocumentFragment() { return makeRef<DocumentFragment>(this); }

void Document::updateWellKnownElements() {
    documentElement_ = nullptr;
    head_ = nullptr;
    body_ = nullptr;
    for (size_t i = 0; i < childCount(); ++i) {
        Node* child = childAt(i);
        if (child->isElement() && static_cast<Element*>(child)->knownTag() == html::HtmlTag::Html) {
            documentElement_ = static_cast<Element*>(child);
            break;
        }
    }
    if (!documentElement_) {
        documentElement_ = firstElementChild();
    }
    if (!documentElement_) {
        return;
    }
    for (Element* child : documentElement_->childElements()) {
        if (child->knownTag() == html::HtmlTag::Head && !head_) {
            head_ = child;
        } else if (child->knownTag() == html::HtmlTag::Body && !body_) {
            body_ = child;
        }
    }
}

std::string Document::title() const {
    if (!head_) {
        return {};
    }
    for (Element* child : head_->childElements()) {
        if (child->knownTag() == html::HtmlTag::Title) {
            return child->textContent();
        }
    }
    return {};
}

void Document::registerId(const Atom& id, Element& element) {
    std::vector<Element*>& elements = idMap_[id];
    if (std::find(elements.begin(), elements.end(), &element) != elements.end()) {
        return;
    }
    elements.push_back(&element);
}

void Document::unregisterId(const Atom& id, Element& element) {
    const auto it = idMap_.find(id);
    if (it == idMap_.end()) {
        return;
    }
    std::vector<Element*>& elements = it->second;
    elements.erase(std::remove(elements.begin(), elements.end(), &element), elements.end());
    if (elements.empty()) {
        idMap_.erase(it);
    }
}

Element* Document::getElementById(const Atom& id) const {
    const auto it = idMap_.find(id);
    if (it == idMap_.end() || it->second.empty()) {
        return nullptr;
    }
    if (it->second.size() == 1) {
        return it->second.front();
    }
    // Several elements share the id: the first in tree order wins.
    Element* best = nullptr;
    size_t bestDepth = 0;
    for (Node* node = const_cast<Document*>(this); node; node = nextInTreeOrder(node, this)) {
        if (!node->isElement()) {
            continue;
        }
        Element* element = static_cast<Element*>(node);
        if (element->id() == id) {
            best = element;
            break;
        }
        ++bestDepth;
    }
    return best ? best : it->second.front();
}

std::vector<Element*> Document::getElementsByTagName(std::string_view tagName) const {
    const Atom wanted = Atom::lowered(tagName);
    const bool all = tagName == "*";
    std::vector<Element*> result;
    for (Node* node = const_cast<Document*>(this); node; node = nextInTreeOrder(node, this)) {
        if (!node->isElement()) {
            continue;
        }
        Element* element = static_cast<Element*>(node);
        if (all || element->tagName() == wanted) {
            result.push_back(element);
        }
    }
    return result;
}

std::vector<Element*> Document::getElementsByClassName(std::string_view className) const {
    const Atom wanted(className);
    std::vector<Element*> result;
    for (Node* node = const_cast<Document*>(this); node; node = nextInTreeOrder(node, this)) {
        if (!node->isElement()) {
            continue;
        }
        Element* element = static_cast<Element*>(node);
        if (element->hasClass(wanted)) {
            result.push_back(element);
        }
    }
    return result;
}

void Document::notifyInserted(Node& node) {
    if (sink_) {
        sink_->onNodeInserted(node);
    }
}

void Document::notifyRemoved(Node& node, Node& formerParent) {
    if (sink_) {
        sink_->onNodeRemoved(node, formerParent);
    }
}

void Document::notifyAttributeChanged(Element& element, const Atom& name) {
    if (sink_) {
        sink_->onAttributeChanged(element, name);
    }
}

void Document::notifyTextChanged(CharacterData& node) {
    if (sink_) {
        sink_->onTextChanged(node);
    }
}

Node* nextInTreeOrder(Node* node, const Node* root) {
    if (!node) {
        return nullptr;
    }
    if (Node* child = node->firstChild()) {
        return child;
    }
    for (Node* current = node; current && current != root; current = current->parentNode()) {
        if (Node* sibling = current->nextSibling()) {
            return sibling;
        }
    }
    return nullptr;
}

Element* nextElementInTreeOrder(Node* node, const Node* root) {
    for (Node* next = nextInTreeOrder(node, root); next; next = nextInTreeOrder(next, root)) {
        if (next->isElement()) {
            return static_cast<Element*>(next);
        }
    }
    return nullptr;
}

} // namespace xgu::dom
