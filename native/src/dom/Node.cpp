#include "dom/Node.h"

#include "dom/Document.h"
#include "dom/Element.h"

namespace xgu::dom {

Node::~Node() {
    for (RefPtr<Node>& child : children_) {
        child->parent_ = nullptr;
        child->connected_ = false;
    }
}

std::string Node::nodeName() const {
    switch (type_) {
    case NodeType::Text:
        return "#text";
    case NodeType::Comment:
        return "#comment";
    case NodeType::Document:
        return "#document";
    case NodeType::DocumentFragment:
        return "#document-fragment";
    case NodeType::Element:
        break;
    }
    return "#unknown";
}

Element* Node::parentElement() const {
    return parent_ && parent_->isElement() ? static_cast<Element*>(parent_) : nullptr;
}

Node* Node::nextSibling() const {
    if (!parent_) {
        return nullptr;
    }
    const size_t next = indexInParent_ + 1;
    return next < parent_->children_.size() ? parent_->children_[next].get() : nullptr;
}

Node* Node::previousSibling() const {
    if (!parent_ || indexInParent_ == 0) {
        return nullptr;
    }
    return parent_->children_[indexInParent_ - 1].get();
}

Element* Node::firstElementChild() const {
    for (const RefPtr<Node>& child : children_) {
        if (child->isElement()) {
            return static_cast<Element*>(child.get());
        }
    }
    return nullptr;
}

bool Node::contains(const Node& other) const {
    for (const Node* node = &other; node; node = node->parent_) {
        if (node == this) {
            return true;
        }
    }
    return false;
}

void Node::reindexFrom(size_t index) {
    for (size_t i = index; i < children_.size(); ++i) {
        children_[i]->indexInParent_ = i;
    }
}

void Node::setConnectedRecursive(bool connected) {
    if (connected_ == connected) {
        return;
    }
    connected_ = connected;
    if (connected) {
        didConnect();
    } else {
        didDisconnect();
    }
    for (RefPtr<Node>& child : children_) {
        child->setConnectedRecursive(connected);
    }
}

bool Node::appendChild(Node& child) { return insertBefore(child, nullptr); }

bool Node::insertBefore(Node& child, Node* referenceChild) {
    if (!isContainer() || &child == this || child.contains(*this)) {
        return false;
    }
    if (referenceChild && referenceChild->parent_ != this) {
        return false;
    }
    // Keep the node alive while it moves between parents.
    RefPtr<Node> protector(&child);
    if (child.parent_) {
        child.parent_->removeChild(child);
    }

    const size_t index = referenceChild ? referenceChild->indexInParent_ : children_.size();
    children_.insert(children_.begin() + static_cast<ptrdiff_t>(index), protector);
    child.parent_ = this;
    child.document_ = document_ ? document_ : (isDocument() ? static_cast<Document*>(this) : nullptr);
    reindexFrom(index);

    const bool nowConnected = isDocument() || connected_;
    if (nowConnected) {
        child.setConnectedRecursive(true);
    }
    markDirty(kDirtyLayoutTree | kDirtyStyleChildren | kDirtyPaintChildren);
    if (Document* doc = document_ ? document_ : (isDocument() ? static_cast<Document*>(this) : nullptr)) {
        doc->bumpTreeVersion();
        doc->notifyInserted(child);
    }
    return true;
}

bool Node::removeChild(Node& child) {
    if (child.parent_ != this) {
        return false;
    }
    RefPtr<Node> protector(&child);
    const size_t index = child.indexInParent_;
    children_.erase(children_.begin() + static_cast<ptrdiff_t>(index));
    child.parent_ = nullptr;
    child.indexInParent_ = 0;
    child.setConnectedRecursive(false);
    reindexFrom(index);
    markDirty(kDirtyLayoutTree | kDirtyStyleChildren | kDirtyPaintChildren);
    if (Document* doc = child.document_) {
        doc->bumpTreeVersion();
        doc->notifyRemoved(child, *this);
    }
    return true;
}

void Node::removeAllChildren() {
    while (!children_.empty()) {
        removeChild(*children_.back());
    }
}

void Node::remove() {
    if (parent_) {
        parent_->removeChild(*this);
    }
}

std::string Node::textContent() const {
    if (type_ == NodeType::Text || type_ == NodeType::Comment) {
        return static_cast<const CharacterData*>(this)->data();
    }
    std::string result;
    for (const RefPtr<Node>& child : children_) {
        if (child->isComment()) {
            continue;
        }
        result += child->textContent();
    }
    return result;
}

void Node::setTextContent(std::string_view text) {
    if (type_ == NodeType::Text || type_ == NodeType::Comment) {
        static_cast<CharacterData*>(this)->setData(text);
        return;
    }
    removeAllChildren();
    if (text.empty()) {
        return;
    }
    Document* doc = document_ ? document_ : (isDocument() ? static_cast<Document*>(this) : nullptr);
    if (!doc) {
        return;
    }
    RefPtr<Text> node = doc->createTextNode(text);
    appendChild(*node);
}

void Node::markDirty(uint8_t bits) {
    dirty_ = static_cast<uint8_t>(dirty_ | bits);
    // Propagate the "children changed" flags towards the root so a style or
    // layout pass can skip clean subtrees.
    constexpr uint8_t kPropagated = kDirtyStyleChildren | kDirtyPaintChildren | kDirtyLayoutTree;
    const uint8_t propagate = static_cast<uint8_t>(bits & kPropagated);
    if (propagate == 0) {
        return;
    }
    // No early-out when an ancestor already carries the flags: clearDirty may be
    // called on any node, so "an ancestor has it" does not imply the root does.
    for (Node* ancestor = parent_; ancestor; ancestor = ancestor->parent_) {
        ancestor->dirty_ = static_cast<uint8_t>(ancestor->dirty_ | propagate);
    }
}

void CharacterData::setData(std::string_view data) {
    if (data_ == data) {
        return;
    }
    data_.assign(data);
    markDirty(kDirtyLayout | kDirtyPaintSelf);
    if (Document* doc = document()) {
        doc->notifyTextChanged(*this);
    }
}

} // namespace xgu::dom
