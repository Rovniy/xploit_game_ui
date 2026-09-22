#pragma once

#include "core/Atom.h"
#include "core/RefCounted.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xgu::dom {

class Document;
class Element;

enum class NodeType : uint8_t {
    Element = 1,
    Text = 3,
    Comment = 8,
    Document = 9,
    DocumentFragment = 11,
};

// Invalidation flags. Style/layout/paint stages consume them from Stage 4 on.
enum DirtyBits : uint8_t {
    kDirtyNone = 0,
    kDirtyStyleSelf = 1 << 0,
    kDirtyStyleChildren = 1 << 1,
    kDirtyLayoutTree = 1 << 2,
    kDirtyLayout = 1 << 3,
    kDirtyPaintSelf = 1 << 4,
    kDirtyPaintChildren = 1 << 5,
};

// Opaque, self-destroying slot the JavaScript bindings use to cache a node's
// wrapper object. The DOM knows nothing about V8.
struct WrapperSlot {
    void* data = nullptr;
    void (*destroy)(void*) = nullptr;

    WrapperSlot() = default;
    WrapperSlot(const WrapperSlot&) = delete;
    WrapperSlot& operator=(const WrapperSlot&) = delete;
    ~WrapperSlot() { clear(); }

    void clear() {
        // Reset first: the callback may release the last reference to the node
        // that owns this slot, and nothing may touch `this` afterwards.
        void* owned = data;
        void (*deleter)(void*) = destroy;
        data = nullptr;
        destroy = nullptr;
        if (owned && deleter) {
            deleter(owned);
        }
    }
};

// Base class of every node. Children are owned by their parent (strong), the
// parent pointer is raw. Single-threaded: runtime thread only.
class Node : public RefCounted {
public:
    NodeType nodeType() const { return type_; }
    bool isElement() const { return type_ == NodeType::Element; }
    bool isText() const { return type_ == NodeType::Text; }
    bool isComment() const { return type_ == NodeType::Comment; }
    bool isDocument() const { return type_ == NodeType::Document; }
    bool isContainer() const { return type_ != NodeType::Text && type_ != NodeType::Comment; }

    // "DIV" for elements, "#text", "#comment", "#document", "#document-fragment".
    virtual std::string nodeName() const;

    Document* document() const { return document_; }
    Node* parentNode() const { return parent_; }
    Element* parentElement() const;
    bool isConnected() const { return connected_; }

    size_t childCount() const { return children_.size(); }
    Node* childAt(size_t index) const { return index < children_.size() ? children_[index].get() : nullptr; }
    Node* firstChild() const { return children_.empty() ? nullptr : children_.front().get(); }
    Node* lastChild() const { return children_.empty() ? nullptr : children_.back().get(); }
    Node* nextSibling() const;
    Node* previousSibling() const;
    Element* firstElementChild() const;
    size_t indexInParent() const { return indexInParent_; }

    // Mutation. `child` may come from another parent; it is moved.
    // Returns false when the operation is invalid (cycle, wrong document, ...).
    bool appendChild(Node& child);
    bool insertBefore(Node& child, Node* referenceChild);
    bool removeChild(Node& child);
    void removeAllChildren();
    // Detaches this node from its parent, if any.
    void remove();
    bool contains(const Node& other) const;

    // Concatenated text of this subtree; setter replaces all children with one
    // text node (or clears them for an empty string).
    std::string textContent() const;
    void setTextContent(std::string_view text);

    uint8_t dirtyBits() const { return dirty_; }
    void markDirty(uint8_t bits);
    void clearDirty(uint8_t bits) { dirty_ = static_cast<uint8_t>(dirty_ & ~bits); }

    WrapperSlot& wrapperSlot() { return wrapper_; }

protected:
    Node(NodeType type, Document* document) : type_(type), document_(document) {}
    ~Node() override;

    virtual void didConnect() {}
    virtual void didDisconnect() {}

private:
    void setConnectedRecursive(bool connected);
    void reindexFrom(size_t index);

    NodeType type_;
    Document* document_ = nullptr;
    Node* parent_ = nullptr;
    size_t indexInParent_ = 0;
    bool connected_ = false;
    uint8_t dirty_ = kDirtyNone;
    std::vector<RefPtr<Node>> children_;
    WrapperSlot wrapper_;
};

class CharacterData : public Node {
public:
    const std::string& data() const { return data_; }
    void setData(std::string_view data);

protected:
    CharacterData(NodeType type, Document* document, std::string_view data) : Node(type, document), data_(data) {}

private:
    std::string data_;
};

class Text final : public CharacterData {
public:
    Text(Document* document, std::string_view data) : CharacterData(NodeType::Text, document, data) {}
    std::string nodeName() const override { return "#text"; }
};

class Comment final : public CharacterData {
public:
    Comment(Document* document, std::string_view data) : CharacterData(NodeType::Comment, document, data) {}
    std::string nodeName() const override { return "#comment"; }
};

class DocumentFragment final : public Node {
public:
    explicit DocumentFragment(Document* document) : Node(NodeType::DocumentFragment, document) {}
    std::string nodeName() const override { return "#document-fragment"; }
};

} // namespace xgu::dom
