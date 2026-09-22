#pragma once

#include <v8.h>

#include <array>

namespace xgu::dom {
class Document;
class Element;
class Node;
} // namespace xgu::dom

namespace xgu::js {

class V8Runtime;

// Exposes the DOM to JavaScript: `document`, the Node/Element/Text wrappers and
// the collection helpers. One instance per isolate, owned by V8Runtime.
//
// Wrapper lifetime: a wrapper object owns a strong reference to its node, while
// the node's WrapperSlot keeps a *weak* handle back to the wrapper. The cycle is
// therefore broken by the weak side: once JavaScript drops the wrapper, the
// reference on the node is released; a node still in the tree stays alive
// through its parent.
class DomBindings {
public:
    enum class Interface : uint8_t { Node, Element, Text, Comment, Document, TokenList, Count };

    DomBindings(V8Runtime& runtime, v8::Isolate* isolate);
    ~DomBindings();

    // Installs `document` (and the DOM constructors) on the context's global.
    void install(v8::Local<v8::Context> context, dom::Document& document);
    void dispose();

    // Returns the wrapper for `node`, creating it on first use. Null nodes map
    // to JavaScript null.
    v8::Local<v8::Value> wrap(v8::Local<v8::Context> context, dom::Node* node);
    // Node behind a wrapper object, or nullptr when `value` is not a wrapper.
    static dom::Node* unwrap(v8::Local<v8::Value> value);

    // Transient DOMTokenList view over an element (Element.classList).
    v8::Local<v8::Value> wrapTokenList(v8::Local<v8::Context> context, dom::Element& element);

    v8::Isolate* isolate() const { return isolate_; }
    V8Runtime& runtime() const { return runtime_; }
    dom::Document* document() const { return document_; }

private:
    v8::Local<v8::FunctionTemplate> templateFor(Interface interface);
    v8::Local<v8::FunctionTemplate> makeTemplate(Interface interface);

    V8Runtime& runtime_;
    v8::Isolate* isolate_;
    dom::Document* document_ = nullptr;
    std::array<v8::Global<v8::FunctionTemplate>, static_cast<size_t>(Interface::Count)> templates_;
};

} // namespace xgu::js
