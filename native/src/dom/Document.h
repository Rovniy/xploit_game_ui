#pragma once

#include "dom/Element.h"
#include "dom/BoxProvider.h"
#include "dom/FocusController.h"
#include "dom/Node.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace xgu {
class IAssetLoader;
}

namespace xgu::css {
class ElementStateProvider;
}

namespace xgu::dom {

class Document;

// Notified on every tree change so the style engine, layout and the JavaScript
// bindings can invalidate what they cache. Stage 4 attaches the style engine.
class MutationSink {
public:
    virtual ~MutationSink() = default;
    virtual void onNodeInserted(Node& node) {}
    virtual void onNodeRemoved(Node& node, Node& formerParent) {}
    virtual void onAttributeChanged(Element& element, const Atom& name) {}
    virtual void onTextChanged(CharacterData& node) {}
};

class Document final : public Node {
public:
    Document();
    ~Document() override;

    std::string nodeName() const override { return "#document"; }

    Element* documentElement() const { return documentElement_; }
    Element* head() const { return head_; }
    Element* body() const { return body_; }
    // Re-scans <html>/<head>/<body> after a parse.
    void updateWellKnownElements();

    // Path of this document relative to the UI root ("UI/Menu/index.html").
    const std::string& url() const { return url_; }
    void setUrl(std::string url) { url_ = std::move(url); }
    std::string title() const;

    IAssetLoader* assetLoader() const { return assetLoader_; }
    void setAssetLoader(IAssetLoader* loader) { assetLoader_ = loader; }

    MutationSink* mutationSink() const { return sink_; }
    void setMutationSink(MutationSink* sink) { sink_ = sink; }

    // Interaction state behind :hover, :active, :focus and :focus-within,
    // provided by the input router. Style recalculation and Element.matches
    // both read it here, so they can never disagree.
    const css::ElementStateProvider* elementStateProvider() const { return elementState_; }
    void setElementStateProvider(const css::ElementStateProvider* provider) { elementState_ = provider; }

    // Where element.focus()/blur() go; the input router installs itself here.
    FocusController* focusController() const { return focus_; }
    void setFocusController(FocusController* controller) { focus_ = controller; }

    // Set by Node::markDirty whenever anything in this document changes, and
    // cleared once a frame has been produced from it. It is what lets an idle
    // view skip the whole restyle, layout, record and rasterise chain.
    bool dirty() const { return dirty_; }
    void noteDirty() { dirty_ = true; }
    void clearDirtyFlag() { dirty_ = false; }

    // Where the geometry and scroll properties read from; the layout engine
    // installs itself here.
    const BoxProvider* boxProvider() const { return boxes_; }
    void setBoxProvider(const BoxProvider* provider) { boxes_ = provider; }

    RefPtr<Element> createElement(std::string_view tagName);
    RefPtr<Text> createTextNode(std::string_view data);
    RefPtr<Comment> createComment(std::string_view data);
    RefPtr<DocumentFragment> createDocumentFragment();

    Element* getElementById(const Atom& id) const;
    Element* getElementById(std::string_view id) const { return getElementById(Atom(id)); }
    std::vector<Element*> getElementsByTagName(std::string_view tagName) const;
    std::vector<Element*> getElementsByClassName(std::string_view className) const;

    // Bumped on every structural change; caches can use it to detect staleness.
    uint64_t treeVersion() const { return treeVersion_; }
    void bumpTreeVersion() { ++treeVersion_; }

    // --- called by Node/Element, not by users ---
    void registerId(const Atom& id, Element& element);
    void unregisterId(const Atom& id, Element& element);
    void notifyInserted(Node& node);
    void notifyRemoved(Node& node, Node& formerParent);
    void notifyAttributeChanged(Element& element, const Atom& name);
    void notifyTextChanged(CharacterData& node);

private:
    Element* documentElement_ = nullptr;
    Element* head_ = nullptr;
    Element* body_ = nullptr;
    std::string url_;
    IAssetLoader* assetLoader_ = nullptr;
    MutationSink* sink_ = nullptr;
    const css::ElementStateProvider* elementState_ = nullptr;
    FocusController* focus_ = nullptr;
    const BoxProvider* boxes_ = nullptr;
    bool dirty_ = true;
    uint64_t treeVersion_ = 0;
    // Documents may hold several elements with the same id; the first in tree
    // order wins, matching browsers.
    std::unordered_map<Atom, std::vector<Element*>> idMap_;
};

// Depth-first traversal helpers (pre-order, tree order).
Node* nextInTreeOrder(Node* node, const Node* root);
Element* nextElementInTreeOrder(Node* node, const Node* root);

template <typename Visitor>
void forEachElement(Node& root, Visitor visitor) {
    for (Node* node = &root; node; node = nextInTreeOrder(node, &root)) {
        if (node->isElement()) {
            visitor(static_cast<Element&>(*node));
        }
    }
}

} // namespace xgu::dom
