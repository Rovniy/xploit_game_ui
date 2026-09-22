#include "js/v8/DomBindings.h"

#include "core/Log.h"
#include "css/Selector.h"
#include "css/SelectorMatcher.h"
#include "css/StyleSheet.h"
#include "dom/Document.h"
#include "dom/Element.h"
#include "dom/Event.h"
#include "dom/EventTarget.h"
#include "dom/FocusController.h"
#include "dom/TextControl.h"
#include "dom/Node.h"
#include "js/v8/V8Runtime.h"

#include <algorithm>
#include <string>
#include <vector>

namespace xgu::js {
namespace {

constexpr int kNodePointerField = 0;
constexpr int kTypeTagField = 1;
constexpr int kInternalFieldCount = 2;

// Stored with SetAlignedPointerInInternalField, so the values must be even.
// Without the tag an Event wrapper would unwrap as a Node.
constexpr uintptr_t kTagNode = 2;
constexpr uintptr_t kTagElementView = 4; // DOMTokenList, CSSStyleDeclaration
constexpr uintptr_t kTagEvent = 6;

uintptr_t typeTagOf(v8::Local<v8::Value> value) {
    if (value.IsEmpty() || !value->IsObject()) {
        return 0;
    }
    v8::Local<v8::Object> object = value.As<v8::Object>();
    if (object->InternalFieldCount() < kInternalFieldCount) {
        return 0;
    }
    return reinterpret_cast<uintptr_t>(object->GetAlignedPointerFromInternalField(kTypeTagField));
}

// Keeps a node alive for as long as its JavaScript wrapper exists.
struct WrapperData {
    RefPtr<dom::Node> node;
    v8::Global<v8::Object> handle; // weak
};

void destroyWrapperData(void* pointer) { delete static_cast<WrapperData*>(pointer); }

void secondPassWeakCallback(const v8::WeakCallbackInfo<WrapperData>& info) {
    WrapperData* data = info.GetParameter();
    dom::Node* node = data->node.get();
    if (!node) {
        delete data;
        return;
    }
    // clear() deletes `data` (and may then destroy the node); nothing may be
    // touched afterwards.
    node->wrapperSlot().clear();
}

void firstPassWeakCallback(const v8::WeakCallbackInfo<WrapperData>& info) {
    info.GetParameter()->handle.Reset();
    info.SetSecondPassCallback(secondPassWeakCallback);
}

// Keeps an event alive for as long as its JavaScript wrapper exists. Same
// scheme as WrapperData, but events are not part of the tree.
struct EventWrapperData {
    RefPtr<dom::Event> event;
    v8::Global<v8::Object> handle; // weak
};

void destroyEventWrapperData(void* pointer) { delete static_cast<EventWrapperData*>(pointer); }

void eventSecondPassWeakCallback(const v8::WeakCallbackInfo<EventWrapperData>& info) {
    EventWrapperData* data = info.GetParameter();
    dom::Event* event = data->event.get();
    if (!event) {
        delete data;
        return;
    }
    event->wrapperSlot().clear(); // deletes `data`, then may destroy the event
}

void eventFirstPassWeakCallback(const v8::WeakCallbackInfo<EventWrapperData>& info) {
    info.GetParameter()->handle.Reset();
    info.SetSecondPassCallback(eventSecondPassWeakCallback);
}

std::string toUtf8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
    if (value.IsEmpty()) {
        return {};
    }
    v8::String::Utf8Value utf8(isolate, value);
    return *utf8 ? std::string(*utf8, static_cast<size_t>(utf8.length())) : std::string();
}

v8::Local<v8::String> toV8(v8::Isolate* isolate, std::string_view text) {
    return v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal, static_cast<int>(text.size()))
        .FromMaybe(v8::String::Empty(isolate));
}

// A matcher that knows about :hover/:active/:focus, so selectors behave the same
// from script as they do in the cascade.
css::SelectorMatcher matcherFor(dom::Document* document) {
    return css::SelectorMatcher(document ? document->elementStateProvider() : nullptr);
}

DomBindings* bindingsOf(v8::Isolate* isolate) {
    V8Runtime* runtime = V8Runtime::fromIsolate(isolate);
    return runtime ? runtime->domBindings() : nullptr;
}

void throwTypeError(v8::Isolate* isolate, const char* message) {
    isolate->ThrowException(v8::Exception::TypeError(toV8(isolate, message)));
}

// A listener holding a script function. Two registrations are the same when
// they hold the same function object, which is what removeEventListener needs.
class JsEventListener final : public dom::EventListener {
public:
    JsEventListener(v8::Isolate* isolate, v8::Local<v8::Function> function)
        : isolate_(isolate), function_(isolate, function) {}

    void handleEvent(dom::Event& event) override;

    bool isSameAs(const dom::EventListener& other) const override {
        if (other.typeTag() != tag()) {
            return false;
        }
        const auto& js = static_cast<const JsEventListener&>(other);
        return !function_.IsEmpty() && !js.function_.IsEmpty() && function_ == js.function_;
    }

    const void* typeTag() const override { return tag(); }

    static const void* tag() {
        static const char kTag = 0;
        return &kTag;
    }

private:
    v8::Isolate* isolate_;
    v8::Global<v8::Function> function_;
};

// --- accessors on the receiver ----------------------------------------------

dom::Node* receiverNode(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    if (!node) {
        throwTypeError(info.GetIsolate(), "not a DOM node");
    }
    return node;
}

dom::Node* receiverNode(const v8::PropertyCallbackInfo<v8::Value>& info) {
    return DomBindings::unwrap(info.This());
}

dom::Element* receiverElement(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    if (!node || !node->isElement()) {
        throwTypeError(info.GetIsolate(), "not an Element");
        return nullptr;
    }
    return static_cast<dom::Element*>(node);
}

dom::Element* receiverElement(const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    return node && node->isElement() ? static_cast<dom::Element*>(node) : nullptr;
}

// The element a DOMTokenList (classList) was created for.
dom::Element* tokenListElement(const v8::FunctionCallbackInfo<v8::Value>& info) {
    return DomBindings::unwrapView(info.This());
}

// --- shared helpers ----------------------------------------------------------

v8::Local<v8::Value> wrapNode(v8::Isolate* isolate, dom::Node* node) {
    DomBindings* bindings = bindingsOf(isolate);
    if (!bindings) {
        return v8::Null(isolate);
    }
    return bindings->wrap(isolate->GetCurrentContext(), node);
}

v8::Local<v8::Array> wrapNodeList(v8::Isolate* isolate, const std::vector<dom::Element*>& elements) {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Array> array = v8::Array::New(isolate, static_cast<int>(elements.size()));
    for (size_t i = 0; i < elements.size(); ++i) {
        array->Set(context, static_cast<uint32_t>(i), wrapNode(isolate, elements[i])).Check();
    }
    return array;
}

// Parses a selector argument and reports syntax errors as a JavaScript exception.
bool parseSelectorArgument(const v8::FunctionCallbackInfo<v8::Value>& info, css::SelectorList& out) {
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1) {
        throwTypeError(isolate, "a selector is required");
        return false;
    }
    const std::string text = toUtf8(isolate, info[0]);
    std::string error;
    std::optional<css::SelectorList> parsed = css::SelectorList::parse(text, &error);
    if (!parsed) {
        const std::string message = "'" + text + "' is not a valid selector: " + error;
        isolate->ThrowException(v8::Exception::Error(toV8(isolate, message)));
        return false;
    }
    out = std::move(*parsed);
    return true;
}

// --- Node ---------------------------------------------------------------------

void nodeTypeGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        info.GetReturnValue().Set(static_cast<int32_t>(node->nodeType()));
    }
}

void nodeNameGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), node->nodeName()));
    }
}

void parentNodeGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        info.GetReturnValue().Set(wrapNode(info.GetIsolate(), node->parentNode()));
    }
}

void parentElementGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        info.GetReturnValue().Set(wrapNode(info.GetIsolate(), node->parentElement()));
    }
}

void firstChildGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        info.GetReturnValue().Set(wrapNode(info.GetIsolate(), node->firstChild()));
    }
}

void lastChildGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        info.GetReturnValue().Set(wrapNode(info.GetIsolate(), node->lastChild()));
    }
}

void nextSiblingGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        info.GetReturnValue().Set(wrapNode(info.GetIsolate(), node->nextSibling()));
    }
}

void previousSiblingGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        info.GetReturnValue().Set(wrapNode(info.GetIsolate(), node->previousSibling()));
    }
}

void childNodesGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    if (!node) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Array> array = v8::Array::New(isolate, static_cast<int>(node->childCount()));
    for (size_t i = 0; i < node->childCount(); ++i) {
        array->Set(context, static_cast<uint32_t>(i), wrapNode(isolate, node->childAt(i))).Check();
    }
    info.GetReturnValue().Set(array);
}

void isConnectedGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        info.GetReturnValue().Set(node->isConnected());
    }
}

void textContentGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), node->textContent()));
    }
}

void textContentSetter(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    if (!node) {
        return;
    }
    node->setTextContent(toUtf8(info.GetIsolate(), value));
}

void appendChildCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* parent = receiverNode(info);
    if (!parent) {
        return;
    }
    dom::Node* child = info.Length() > 0 ? DomBindings::unwrap(info[0]) : nullptr;
    if (!child) {
        throwTypeError(info.GetIsolate(), "appendChild expects a node");
        return;
    }
    if (!parent->appendChild(*child)) {
        throwTypeError(info.GetIsolate(), "the node cannot be inserted here");
        return;
    }
    info.GetReturnValue().Set(info[0]);
}

void insertBeforeCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* parent = receiverNode(info);
    if (!parent) {
        return;
    }
    dom::Node* child = info.Length() > 0 ? DomBindings::unwrap(info[0]) : nullptr;
    if (!child) {
        throwTypeError(info.GetIsolate(), "insertBefore expects a node");
        return;
    }
    dom::Node* reference = info.Length() > 1 ? DomBindings::unwrap(info[1]) : nullptr;
    if (!parent->insertBefore(*child, reference)) {
        throwTypeError(info.GetIsolate(), "the node cannot be inserted here");
        return;
    }
    info.GetReturnValue().Set(info[0]);
}

void removeChildCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* parent = receiverNode(info);
    if (!parent) {
        return;
    }
    dom::Node* child = info.Length() > 0 ? DomBindings::unwrap(info[0]) : nullptr;
    if (!child || !parent->removeChild(*child)) {
        throwTypeError(info.GetIsolate(), "removeChild expects a child of this node");
        return;
    }
    info.GetReturnValue().Set(info[0]);
}

void containsCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    if (!node) {
        return;
    }
    dom::Node* other = info.Length() > 0 ? DomBindings::unwrap(info[0]) : nullptr;
    info.GetReturnValue().Set(other != nullptr && node->contains(*other));
}

void removeCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = receiverNode(info)) {
        node->remove();
    }
}

// --- CharacterData -------------------------------------------------------------

void dataGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    if (node && (node->isText() || node->isComment())) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), static_cast<dom::CharacterData*>(node)->data()));
    }
}

void dataSetter(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    if (node && (node->isText() || node->isComment())) {
        static_cast<dom::CharacterData*>(node)->setData(toUtf8(info.GetIsolate(), value));
    }
}

// --- Element -------------------------------------------------------------------

void tagNameGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Element* element = receiverElement(info)) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), element->nodeName()));
    }
}

void idGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Element* element = receiverElement(info)) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), element->id().view()));
    }
}

void idSetter(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    if (node && node->isElement()) {
        static_cast<dom::Element*>(node)->setAttribute(Atom("id"), toUtf8(info.GetIsolate(), value));
    }
}

void classNameGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Element* element = receiverElement(info)) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), element->className()));
    }
}

void classNameSetter(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    if (node && node->isElement()) {
        static_cast<dom::Element*>(node)->setClassName(toUtf8(info.GetIsolate(), value));
    }
}

void childrenGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Element* element = receiverElement(info)) {
        info.GetReturnValue().Set(wrapNodeList(info.GetIsolate(), element->childElements()));
    }
}

void innerHtmlGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Element* element = receiverElement(info)) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), element->innerHTML()));
    }
}

void innerHtmlSetter(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    if (!node || !node->isElement()) {
        return;
    }
    if (!static_cast<dom::Element*>(node)->setInnerHTML(toUtf8(info.GetIsolate(), value))) {
        info.GetIsolate()->ThrowException(v8::Exception::Error(toV8(info.GetIsolate(), "innerHTML: parse failed")));
    }
}

void outerHtmlGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Element* element = receiverElement(info)) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), element->outerHTML()));
    }
}

void getAttributeCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    if (!element || info.Length() < 1) {
        return;
    }
    const std::string* value = element->getAttribute(Atom::lowered(toUtf8(info.GetIsolate(), info[0])));
    if (value) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), *value));
    } else {
        info.GetReturnValue().SetNull();
    }
}

void setAttributeCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    if (!element) {
        return;
    }
    if (info.Length() < 2) {
        throwTypeError(info.GetIsolate(), "setAttribute expects a name and a value");
        return;
    }
    element->setAttribute(Atom::lowered(toUtf8(info.GetIsolate(), info[0])), toUtf8(info.GetIsolate(), info[1]));
}

void removeAttributeCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    if (element && info.Length() >= 1) {
        element->removeAttribute(Atom::lowered(toUtf8(info.GetIsolate(), info[0])));
    }
}

void hasAttributeCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    if (!element || info.Length() < 1) {
        info.GetReturnValue().Set(false);
        return;
    }
    info.GetReturnValue().Set(element->hasAttribute(Atom::lowered(toUtf8(info.GetIsolate(), info[0]))));
}

void matchesCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    css::SelectorList list;
    if (!element || !parseSelectorArgument(info, list)) {
        return;
    }
    info.GetReturnValue().Set(matcherFor(element->document()).matches(*element, list));
}

void querySelectorCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    css::SelectorList list;
    if (!node || !parseSelectorArgument(info, list)) {
        return;
    }
    info.GetReturnValue().Set(wrapNode(info.GetIsolate(), matcherFor(node->document()).queryFirst(*node, list)));
}

void querySelectorAllCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    css::SelectorList list;
    if (!node || !parseSelectorArgument(info, list)) {
        return;
    }
    info.GetReturnValue().Set(wrapNodeList(info.GetIsolate(), matcherFor(node->document()).queryAll(*node, list)));
}

void getElementsByTagNameCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    if (!node || info.Length() < 1) {
        return;
    }
    const std::string name = toUtf8(info.GetIsolate(), info[0]);
    std::vector<dom::Element*> result;
    const Atom wanted = Atom::lowered(name);
    const bool all = name == "*";
    for (dom::Node* current = dom::nextInTreeOrder(node, node); current;
         current = dom::nextInTreeOrder(current, node)) {
        if (current->isElement() && (all || static_cast<dom::Element*>(current)->tagName() == wanted)) {
            result.push_back(static_cast<dom::Element*>(current));
        }
    }
    info.GetReturnValue().Set(wrapNodeList(info.GetIsolate(), result));
}

void classListGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    if (!element) {
        return;
    }
    DomBindings* bindings = bindingsOf(info.GetIsolate());
    if (!bindings) {
        return;
    }
    // A DOMTokenList is a thin view: it wraps the same element with a different
    // prototype, so it needs no lifetime handling of its own.
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    v8::Local<v8::Value> list = bindings->wrapTokenList(context, *element);
    info.GetReturnValue().Set(list);
}

// --- DOMTokenList (classList) ---------------------------------------------------

void tokenListAddCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = tokenListElement(info);
    if (!element) {
        return;
    }
    for (int i = 0; i < info.Length(); ++i) {
        element->addClass(toUtf8(info.GetIsolate(), info[i]));
    }
}

void tokenListRemoveCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = tokenListElement(info);
    if (!element) {
        return;
    }
    for (int i = 0; i < info.Length(); ++i) {
        element->removeClass(toUtf8(info.GetIsolate(), info[i]));
    }
}

void tokenListToggleCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = tokenListElement(info);
    if (!element || info.Length() < 1) {
        return;
    }
    const std::string name = toUtf8(info.GetIsolate(), info[0]);
    if (info.Length() >= 2) {
        const bool force = info[1]->BooleanValue(info.GetIsolate());
        if (force) {
            element->addClass(name);
        } else {
            element->removeClass(name);
        }
        info.GetReturnValue().Set(force);
        return;
    }
    info.GetReturnValue().Set(element->toggleClass(name));
}

void tokenListContainsCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = tokenListElement(info);
    if (!element || info.Length() < 1) {
        info.GetReturnValue().Set(false);
        return;
    }
    info.GetReturnValue().Set(element->hasClass(Atom(toUtf8(info.GetIsolate(), info[0]))));
}

void tokenListLengthGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Element* element = DomBindings::unwrapView(info.This())) {
        info.GetReturnValue().Set(static_cast<uint32_t>(element->classList().size()));
    }
}

void tokenListItemCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = tokenListElement(info);
    if (!element || info.Length() < 1) {
        return;
    }
    const int64_t index = info[0]->IntegerValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(-1);
    const auto& classes = element->classList();
    if (index < 0 || static_cast<size_t>(index) >= classes.size()) {
        info.GetReturnValue().SetNull();
        return;
    }
    info.GetReturnValue().Set(toV8(info.GetIsolate(), classes[static_cast<size_t>(index)].view()));
}

void tokenListValueGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::Element* element = DomBindings::unwrapView(info.This())) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), element->className()));
    }
}

// --- CSSStyleDeclaration (element.style) -------------------------------------

// "backgroundColor" -> "background-color"; a name that is already kebab-case
// passes through unchanged.
std::string toCssPropertyName(std::string_view name) {
    std::string result;
    result.reserve(name.size() + 4);
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') {
            result.push_back('-');
            result.push_back(static_cast<char>(c - 'A' + 'a'));
        } else {
            result.push_back(c);
        }
    }
    return result;
}

dom::Element* styleOwner(v8::Local<v8::Object> holder) {
    dom::Node* node = DomBindings::unwrapView(holder);
    return node && node->isElement() ? static_cast<dom::Element*>(node) : nullptr;
}

// Reads a property out of the inline style block.
std::string readInlineProperty(const dom::Element& element, const std::string& property) {
    const css::DeclarationBlock* block = element.inlineStyle();
    if (!block) {
        return {};
    }
    const css::PropertyId id = css::propertyFromName(property);
    if (id == css::PropertyId::Invalid) {
        return {};
    }
    for (auto it = block->rbegin(); it != block->rend(); ++it) {
        if (it->property == id) {
            return css::serializeValue(it->value);
        }
    }
    return {};
}

bool writeInlineProperty(dom::Element& element, const std::string& property, std::string_view value) {
    css::DeclarationBlock& block = element.ensureInlineStyle();
    const css::PropertyId id = css::propertyFromName(property);

    // Setting a property replaces every earlier declaration of it (and of the
    // longhands a shorthand expands to).
    css::DeclarationBlock parsed;
    if (!value.empty() && !css::parseDeclaration(property, value, parsed)) {
        return false;
    }
    std::vector<css::PropertyId> touched;
    if (parsed.empty()) {
        if (id != css::PropertyId::Invalid) {
            touched.push_back(id);
        }
    } else {
        for (const css::Declaration& declaration : parsed) {
            touched.push_back(declaration.property);
        }
    }
    block.erase(std::remove_if(block.begin(), block.end(),
                               [&](const css::Declaration& declaration) {
                                   return std::find(touched.begin(), touched.end(), declaration.property) !=
                                          touched.end();
                               }),
                block.end());
    block.insert(block.end(), parsed.begin(), parsed.end());
    element.syncInlineStyleAttribute();
    return true;
}

void styleGetPropertyValueCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = styleOwner(info.This());
    if (!element || info.Length() < 1) {
        return;
    }
    const std::string property = toCssPropertyName(toUtf8(info.GetIsolate(), info[0]));
    info.GetReturnValue().Set(toV8(info.GetIsolate(), readInlineProperty(*element, property)));
}

void styleSetPropertyCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = styleOwner(info.This());
    if (!element || info.Length() < 2) {
        return;
    }
    const std::string property = toCssPropertyName(toUtf8(info.GetIsolate(), info[0]));
    writeInlineProperty(*element, property, toUtf8(info.GetIsolate(), info[1]));
}

void styleRemovePropertyCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = styleOwner(info.This());
    if (!element || info.Length() < 1) {
        return;
    }
    const std::string property = toCssPropertyName(toUtf8(info.GetIsolate(), info[0]));
    const std::string previous = readInlineProperty(*element, property);
    writeInlineProperty(*element, property, {});
    info.GetReturnValue().Set(toV8(info.GetIsolate(), previous));
}

void styleCssTextGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Element* element = styleOwner(info.This());
    if (!element) {
        return;
    }
    const css::DeclarationBlock* block = element->inlineStyle();
    info.GetReturnValue().Set(
        toV8(info.GetIsolate(), block ? css::serializeDeclarations(*block) : std::string()));
}

// style.width = "10px" and style.width both go through here. Returning
// kIntercepted tells V8 the property was handled; otherwise the object's own
// methods (setProperty, cssText, ...) resolve normally.
v8::Intercepted styleNamedGetter(v8::Local<v8::Name> name, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (!name->IsString()) {
        return v8::Intercepted::kNo;
    }
    dom::Element* element = styleOwner(info.This());
    if (!element) {
        return v8::Intercepted::kNo;
    }
    const std::string property = toCssPropertyName(toUtf8(info.GetIsolate(), name));
    if (css::propertyFromName(property) == css::PropertyId::Invalid) {
        // A shorthand reads back as the empty string: it has no stored value of
        // its own and the MVP does not re-serialise one from its longhands.
        if (css::isShorthandName(property)) {
            info.GetReturnValue().Set(toV8(info.GetIsolate(), std::string()));
            return v8::Intercepted::kYes;
        }
        return v8::Intercepted::kNo;
    }
    info.GetReturnValue().Set(toV8(info.GetIsolate(), readInlineProperty(*element, property)));
    return v8::Intercepted::kYes;
}

v8::Intercepted styleNamedSetter(v8::Local<v8::Name> name, v8::Local<v8::Value> value,
                                 const v8::PropertyCallbackInfo<void>& info) {
    if (!name->IsString()) {
        return v8::Intercepted::kNo;
    }
    dom::Element* element = styleOwner(info.This());
    if (!element) {
        return v8::Intercepted::kNo;
    }
    const std::string property = toCssPropertyName(toUtf8(info.GetIsolate(), name));
    // Shorthands have no PropertyId of their own; writeInlineProperty expands them.
    if (css::propertyFromName(property) == css::PropertyId::Invalid && !css::isShorthandName(property)) {
        return v8::Intercepted::kNo;
    }
    writeInlineProperty(*element, property, toUtf8(info.GetIsolate(), value));
    return v8::Intercepted::kYes;
}

void styleGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    if (!element) {
        return;
    }
    DomBindings* bindings = bindingsOf(info.GetIsolate());
    if (!bindings) {
        return;
    }
    info.GetReturnValue().Set(bindings->wrapStyleDeclaration(info.GetIsolate()->GetCurrentContext(), *element));
}

// --- EventTarget ------------------------------------------------------------------

// addEventListener(type, handler, options). `options` may be a boolean (the
// capture flag) or an object with `capture` and `once`.
void readListenerOptions(v8::Local<v8::Context> context, v8::Local<v8::Value> options, bool& capture, bool& once) {
    v8::Isolate* isolate = context->GetIsolate();
    if (options.IsEmpty() || options->IsUndefined() || options->IsNull()) {
        return;
    }
    if (options->IsBoolean()) {
        capture = options->BooleanValue(isolate);
        return;
    }
    if (!options->IsObject()) {
        return;
    }
    v8::Local<v8::Object> object = options.As<v8::Object>();
    v8::Local<v8::Value> value;
    if (object->Get(context, toV8(isolate, "capture")).ToLocal(&value) && !value->IsUndefined()) {
        capture = value->BooleanValue(isolate);
    }
    if (object->Get(context, toV8(isolate, "once")).ToLocal(&value) && !value->IsUndefined()) {
        once = value->BooleanValue(isolate);
    }
}

// The node an EventTarget method was called on. `window` forwards to the
// document, so a receiver that is not a wrapper resolves to it.
dom::Node* eventTargetReceiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (dom::Node* node = DomBindings::unwrap(info.This())) {
        return node;
    }
    DomBindings* bindings = bindingsOf(info.GetIsolate());
    return bindings ? static_cast<dom::Node*>(bindings->document()) : nullptr;
}

void addEventListenerCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    dom::Node* node = eventTargetReceiver(info);
    if (!node || info.Length() < 2) {
        return;
    }
    if (!info[1]->IsFunction()) {
        return; // objects with handleEvent are not supported
    }
    bool capture = false;
    bool once = false;
    readListenerOptions(isolate->GetCurrentContext(), info.Length() > 2 ? info[2] : v8::Local<v8::Value>(), capture,
                        once);
    const Atom type(toUtf8(isolate, info[0]));
    node->addEventListener(type, makeRef<JsEventListener>(isolate, info[1].As<v8::Function>()), capture, once);
}

void removeEventListenerCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    dom::Node* node = eventTargetReceiver(info);
    if (!node || info.Length() < 2 || !info[1]->IsFunction()) {
        return;
    }
    bool capture = false;
    bool once = false;
    readListenerOptions(isolate->GetCurrentContext(), info.Length() > 2 ? info[2] : v8::Local<v8::Value>(), capture,
                        once);
    const Atom type(toUtf8(isolate, info[0]));
    JsEventListener probe(isolate, info[1].As<v8::Function>());
    node->removeEventListener(type, probe, capture);
}

void dispatchEventCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* node = eventTargetReceiver(info);
    dom::Event* event = info.Length() > 0 ? DomBindings::unwrapEvent(info[0]) : nullptr;
    if (!node || !event) {
        throwTypeError(info.GetIsolate(), "dispatchEvent expects an Event");
        return;
    }
    info.GetReturnValue().Set(dom::dispatchEvent(*node, *event));
}

// --- Event ------------------------------------------------------------------------

dom::Event* receiverEvent(const v8::PropertyCallbackInfo<v8::Value>& info) {
    return DomBindings::unwrapEvent(info.This());
}

dom::Event* receiverEvent(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Event* event = DomBindings::unwrapEvent(info.This());
    if (!event) {
        throwTypeError(info.GetIsolate(), "not an Event");
    }
    return event;
}

#define XGU_EVENT_GETTER(name, expression)                                                                           \
    void name(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {                                \
        dom::Event* event = receiverEvent(info);                                                                     \
        if (!event) {                                                                                                \
            return;                                                                                                  \
        }                                                                                                            \
        info.GetReturnValue().Set(expression);                                                                        \
    }

XGU_EVENT_GETTER(eventTypeGetter, toV8(info.GetIsolate(), event->type().view()))
XGU_EVENT_GETTER(eventBubblesGetter, event->bubbles())
XGU_EVENT_GETTER(eventCancelableGetter, event->cancelable())
XGU_EVENT_GETTER(eventPhaseGetter, static_cast<int32_t>(event->phase()))
XGU_EVENT_GETTER(eventDefaultPreventedGetter, event->defaultPrevented())
XGU_EVENT_GETTER(eventTimeStampGetter, event->timeStamp() * 1000.0)
XGU_EVENT_GETTER(eventTargetGetter, wrapNode(info.GetIsolate(), event->target()))
XGU_EVENT_GETTER(eventCurrentTargetGetter, wrapNode(info.GetIsolate(), event->currentTarget()))
XGU_EVENT_GETTER(eventRelatedTargetGetter, wrapNode(info.GetIsolate(), event->relatedTarget()))
XGU_EVENT_GETTER(eventClientXGetter, event->clientX)
XGU_EVENT_GETTER(eventClientYGetter, event->clientY)
XGU_EVENT_GETTER(eventMovementXGetter, event->movementX)
XGU_EVENT_GETTER(eventMovementYGetter, event->movementY)
XGU_EVENT_GETTER(eventButtonGetter, event->button)
XGU_EVENT_GETTER(eventButtonsGetter, event->buttons)
XGU_EVENT_GETTER(eventDetailGetter, event->detail)
XGU_EVENT_GETTER(eventDeltaXGetter, event->deltaX)
XGU_EVENT_GETTER(eventDeltaYGetter, event->deltaY)
XGU_EVENT_GETTER(eventKeyGetter, toV8(info.GetIsolate(), event->key))
XGU_EVENT_GETTER(eventCodeGetter, toV8(info.GetIsolate(), event->code))
XGU_EVENT_GETTER(eventRepeatGetter, event->repeat)
XGU_EVENT_GETTER(eventDataGetter, toV8(info.GetIsolate(), event->data))
XGU_EVENT_GETTER(eventInputTypeGetter, toV8(info.GetIsolate(), event->inputType))
XGU_EVENT_GETTER(eventAltKeyGetter, event->modifiers.alt)
XGU_EVENT_GETTER(eventCtrlKeyGetter, event->modifiers.ctrl)
XGU_EVENT_GETTER(eventShiftKeyGetter, event->modifiers.shift)
XGU_EVENT_GETTER(eventMetaKeyGetter, event->modifiers.meta)

#undef XGU_EVENT_GETTER

// deltaMode is always 0 (pixels): the host converts wheel notches for us.
void eventDeltaModeGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    info.GetReturnValue().Set(0);
}

void eventPreventDefaultCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (dom::Event* event = receiverEvent(info)) {
        event->preventDefault();
    }
}

void eventStopPropagationCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (dom::Event* event = receiverEvent(info)) {
        event->stopPropagation();
    }
}

void eventStopImmediatePropagationCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (dom::Event* event = receiverEvent(info)) {
        event->stopImmediatePropagation();
    }
}

// new Event(type, { bubbles, cancelable })
void eventConstructorCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    if (!info.IsConstructCall()) {
        throwTypeError(isolate, "Event requires 'new'");
        return;
    }
    DomBindings* bindings = bindingsOf(isolate);
    if (!bindings || info.Length() < 1) {
        throwTypeError(isolate, "Event requires a type");
        return;
    }
    bool bubbles = false;
    bool cancelable = false;
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() > 1 && info[1]->IsObject()) {
        v8::Local<v8::Object> init = info[1].As<v8::Object>();
        v8::Local<v8::Value> value;
        if (init->Get(context, toV8(isolate, "bubbles")).ToLocal(&value)) {
            bubbles = value->BooleanValue(isolate);
        }
        if (init->Get(context, toV8(isolate, "cancelable")).ToLocal(&value)) {
            cancelable = value->BooleanValue(isolate);
        }
    }
    RefPtr<dom::Event> event =
        makeRef<dom::Event>(Atom(toUtf8(isolate, info[0])), dom::EventCategory::Plain, bubbles, cancelable);
    info.GetReturnValue().Set(bindings->wrapEvent(context, event.get()));
}

// --- form controls ----------------------------------------------------------------

// The editing state of the receiver, or null when it is not a text control.
dom::TextControl* receiverControl(const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    return element && element->isTextControl() ? &element->ensureTextControl() : nullptr;
}

void valueGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::TextControl* control = receiverControl(info)) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), control->value()));
    }
}

void valueSetter(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    if (!node || !node->isElement()) {
        return;
    }
    auto* element = static_cast<dom::Element*>(node);
    if (!element->isTextControl()) {
        return;
    }
    if (element->ensureTextControl().setValue(toUtf8(info.GetIsolate(), value))) {
        // The value drives layout, so the box tree is rebuilt from it.
        element->markDirty(dom::kDirtyLayoutTree | dom::kDirtyLayout | dom::kDirtyPaintSelf);
    }
}

void selectionStartGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::TextControl* control = receiverControl(info)) {
        info.GetReturnValue().Set(static_cast<uint32_t>(control->utf16ToUtf8Offset(control->selectionStart())));
    }
}

void selectionEndGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (dom::TextControl* control = receiverControl(info)) {
        info.GetReturnValue().Set(static_cast<uint32_t>(control->utf16ToUtf8Offset(control->selectionEnd())));
    }
}

void setSelectionRangeCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    if (!element || !element->isTextControl() || info.Length() < 2) {
        return;
    }
    v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
    dom::TextControl& control = element->ensureTextControl();
    const auto start = static_cast<size_t>(std::max<int64_t>(0, info[0]->IntegerValue(context).FromMaybe(0)));
    const auto end = static_cast<size_t>(std::max<int64_t>(0, info[1]->IntegerValue(context).FromMaybe(0)));
    control.setSelection(control.utf8ToUtf16Offset(start), control.utf8ToUtf16Offset(end));
    element->markDirty(dom::kDirtyPaintSelf);
}

void selectCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    if (!element || !element->isTextControl()) {
        return;
    }
    element->ensureTextControl().selectAll();
    element->markDirty(dom::kDirtyPaintSelf);
}

void focusCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    if (!element || !element->document() || !element->document()->focusController()) {
        return;
    }
    element->document()->focusController()->requestFocus(element);
}

void blurCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Element* element = receiverElement(info);
    if (!element || !element->document() || !element->document()->focusController()) {
        return;
    }
    dom::FocusController* controller = element->document()->focusController();
    if (controller->focusedElement() == element) {
        controller->requestFocus(nullptr);
    }
}

void activeElementGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    if (!node || !node->isDocument()) {
        return;
    }
    auto* document = static_cast<dom::Document*>(node);
    dom::Element* focused = document->focusController() ? document->focusController()->focusedElement() : nullptr;
    info.GetReturnValue().Set(wrapNode(info.GetIsolate(), focused ? static_cast<dom::Node*>(focused)
                                                                  : static_cast<dom::Node*>(document->body())));
}

// --- Document --------------------------------------------------------------------

void documentElementGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    if (node && node->isDocument()) {
        info.GetReturnValue().Set(wrapNode(info.GetIsolate(), static_cast<dom::Document*>(node)->documentElement()));
    }
}

void documentHeadGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    if (node && node->isDocument()) {
        info.GetReturnValue().Set(wrapNode(info.GetIsolate(), static_cast<dom::Document*>(node)->head()));
    }
}

void documentBodyGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    if (node && node->isDocument()) {
        info.GetReturnValue().Set(wrapNode(info.GetIsolate(), static_cast<dom::Document*>(node)->body()));
    }
}

void documentTitleGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    if (node && node->isDocument()) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), static_cast<dom::Document*>(node)->title()));
    }
}

void documentUrlGetter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    dom::Node* node = receiverNode(info);
    if (node && node->isDocument()) {
        info.GetReturnValue().Set(toV8(info.GetIsolate(), static_cast<dom::Document*>(node)->url()));
    }
}

dom::Document* receiverDocument(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Node* node = DomBindings::unwrap(info.This());
    if (!node || !node->isDocument()) {
        throwTypeError(info.GetIsolate(), "not a Document");
        return nullptr;
    }
    return static_cast<dom::Document*>(node);
}

void getElementByIdCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Document* document = receiverDocument(info);
    if (!document || info.Length() < 1) {
        return;
    }
    info.GetReturnValue().Set(
        wrapNode(info.GetIsolate(), document->getElementById(toUtf8(info.GetIsolate(), info[0]))));
}

void getElementsByClassNameCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Document* document = receiverDocument(info);
    if (!document || info.Length() < 1) {
        return;
    }
    info.GetReturnValue().Set(
        wrapNodeList(info.GetIsolate(), document->getElementsByClassName(toUtf8(info.GetIsolate(), info[0]))));
}

void createElementCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Document* document = receiverDocument(info);
    if (!document) {
        return;
    }
    if (info.Length() < 1) {
        throwTypeError(info.GetIsolate(), "createElement expects a tag name");
        return;
    }
    RefPtr<dom::Element> element = document->createElement(toUtf8(info.GetIsolate(), info[0]));
    info.GetReturnValue().Set(wrapNode(info.GetIsolate(), element.get()));
}

void createTextNodeCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Document* document = receiverDocument(info);
    if (!document) {
        return;
    }
    RefPtr<dom::Text> text =
        document->createTextNode(info.Length() > 0 ? toUtf8(info.GetIsolate(), info[0]) : std::string());
    info.GetReturnValue().Set(wrapNode(info.GetIsolate(), text.get()));
}

void createCommentCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    dom::Document* document = receiverDocument(info);
    if (!document) {
        return;
    }
    RefPtr<dom::Comment> comment =
        document->createComment(info.Length() > 0 ? toUtf8(info.GetIsolate(), info[0]) : std::string());
    info.GetReturnValue().Set(wrapNode(info.GetIsolate(), comment.get()));
}

} // namespace

// ---------------------------------------------------------------------------

DomBindings::DomBindings(V8Runtime& runtime, v8::Isolate* isolate) : runtime_(runtime), isolate_(isolate) {}

DomBindings::~DomBindings() { dispose(); }

void DomBindings::dispose() {
    for (auto& handle : templates_) {
        handle.Reset();
    }
    document_ = nullptr;
}

v8::Local<v8::FunctionTemplate> DomBindings::templateFor(Interface interface) {
    const size_t index = static_cast<size_t>(interface);
    if (!templates_[index].IsEmpty()) {
        return templates_[index].Get(isolate_);
    }
    v8::Local<v8::FunctionTemplate> created = makeTemplate(interface);
    templates_[index].Reset(isolate_, created);
    return created;
}

v8::Local<v8::FunctionTemplate> DomBindings::makeTemplate(Interface interface) {
    v8::EscapableHandleScope scope(isolate_);
    v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate_);
    v8::Local<v8::ObjectTemplate> instance = tmpl->InstanceTemplate();
    instance->SetInternalFieldCount(kInternalFieldCount);
    v8::Local<v8::ObjectTemplate> proto = tmpl->PrototypeTemplate();

    const auto accessor = [&](v8::Local<v8::ObjectTemplate> target, const char* name,
                              v8::AccessorNameGetterCallback getter, v8::AccessorNameSetterCallback setter = nullptr) {
        target->SetNativeDataProperty(toV8(isolate_, name), getter, setter, v8::Local<v8::Value>(), v8::DontDelete);
    };
    const auto method = [&](v8::Local<v8::ObjectTemplate> target, const char* name, v8::FunctionCallback callback) {
        target->Set(isolate_, name, v8::FunctionTemplate::New(isolate_, callback));
    };

    switch (interface) {
    case Interface::EventTarget:
        tmpl->SetClassName(toV8(isolate_, "EventTarget"));
        method(proto, "addEventListener", addEventListenerCallback);
        method(proto, "removeEventListener", removeEventListenerCallback);
        method(proto, "dispatchEvent", dispatchEventCallback);
        break;
    case Interface::Node:
        tmpl->SetClassName(toV8(isolate_, "Node"));
        tmpl->Inherit(templateFor(Interface::EventTarget));
        accessor(instance, "nodeType", nodeTypeGetter);
        accessor(instance, "nodeName", nodeNameGetter);
        accessor(instance, "parentNode", parentNodeGetter);
        accessor(instance, "parentElement", parentElementGetter);
        accessor(instance, "firstChild", firstChildGetter);
        accessor(instance, "lastChild", lastChildGetter);
        accessor(instance, "nextSibling", nextSiblingGetter);
        accessor(instance, "previousSibling", previousSiblingGetter);
        accessor(instance, "childNodes", childNodesGetter);
        accessor(instance, "isConnected", isConnectedGetter);
        accessor(instance, "textContent", textContentGetter, textContentSetter);
        method(proto, "appendChild", appendChildCallback);
        method(proto, "insertBefore", insertBeforeCallback);
        method(proto, "removeChild", removeChildCallback);
        method(proto, "contains", containsCallback);
        method(proto, "querySelector", querySelectorCallback);
        method(proto, "querySelectorAll", querySelectorAllCallback);
        method(proto, "getElementsByTagName", getElementsByTagNameCallback);
        break;
    case Interface::Element:
        tmpl->SetClassName(toV8(isolate_, "Element"));
        tmpl->Inherit(templateFor(Interface::Node));
        accessor(instance, "tagName", tagNameGetter);
        accessor(instance, "id", idGetter, idSetter);
        accessor(instance, "className", classNameGetter, classNameSetter);
        accessor(instance, "classList", classListGetter);
        accessor(instance, "style", styleGetter);
        accessor(instance, "children", childrenGetter);
        accessor(instance, "innerHTML", innerHtmlGetter, innerHtmlSetter);
        accessor(instance, "outerHTML", outerHtmlGetter);
        method(proto, "getAttribute", getAttributeCallback);
        method(proto, "setAttribute", setAttributeCallback);
        method(proto, "removeAttribute", removeAttributeCallback);
        method(proto, "hasAttribute", hasAttributeCallback);
        method(proto, "matches", matchesCallback);
        method(proto, "remove", removeCallback);
        // Form controls: the accessors return undefined on other elements.
        accessor(instance, "value", valueGetter, valueSetter);
        accessor(instance, "selectionStart", selectionStartGetter);
        accessor(instance, "selectionEnd", selectionEndGetter);
        method(proto, "setSelectionRange", setSelectionRangeCallback);
        method(proto, "select", selectCallback);
        method(proto, "focus", focusCallback);
        method(proto, "blur", blurCallback);
        break;
    case Interface::Text:
        tmpl->SetClassName(toV8(isolate_, "Text"));
        tmpl->Inherit(templateFor(Interface::Node));
        accessor(instance, "data", dataGetter, dataSetter);
        accessor(instance, "nodeValue", dataGetter, dataSetter);
        break;
    case Interface::Comment:
        tmpl->SetClassName(toV8(isolate_, "Comment"));
        tmpl->Inherit(templateFor(Interface::Node));
        accessor(instance, "data", dataGetter, dataSetter);
        accessor(instance, "nodeValue", dataGetter, dataSetter);
        break;
    case Interface::Event:
        tmpl->SetClassName(toV8(isolate_, "Event"));
        tmpl->SetCallHandler(eventConstructorCallback);
        accessor(instance, "type", eventTypeGetter);
        accessor(instance, "bubbles", eventBubblesGetter);
        accessor(instance, "cancelable", eventCancelableGetter);
        accessor(instance, "eventPhase", eventPhaseGetter);
        accessor(instance, "defaultPrevented", eventDefaultPreventedGetter);
        accessor(instance, "timeStamp", eventTimeStampGetter);
        accessor(instance, "target", eventTargetGetter);
        accessor(instance, "srcElement", eventTargetGetter);
        accessor(instance, "currentTarget", eventCurrentTargetGetter);
        method(proto, "preventDefault", eventPreventDefaultCallback);
        method(proto, "stopPropagation", eventStopPropagationCallback);
        method(proto, "stopImmediatePropagation", eventStopImmediatePropagationCallback);
        break;
    case Interface::MouseEvent:
        tmpl->SetClassName(toV8(isolate_, "MouseEvent"));
        tmpl->Inherit(templateFor(Interface::Event));
        accessor(instance, "clientX", eventClientXGetter);
        accessor(instance, "clientY", eventClientYGetter);
        // The view is the whole coordinate space here, so page and screen
        // coordinates are the client ones.
        accessor(instance, "pageX", eventClientXGetter);
        accessor(instance, "pageY", eventClientYGetter);
        accessor(instance, "screenX", eventClientXGetter);
        accessor(instance, "screenY", eventClientYGetter);
        accessor(instance, "offsetX", eventClientXGetter);
        accessor(instance, "offsetY", eventClientYGetter);
        accessor(instance, "movementX", eventMovementXGetter);
        accessor(instance, "movementY", eventMovementYGetter);
        accessor(instance, "button", eventButtonGetter);
        accessor(instance, "buttons", eventButtonsGetter);
        accessor(instance, "detail", eventDetailGetter);
        accessor(instance, "relatedTarget", eventRelatedTargetGetter);
        accessor(instance, "altKey", eventAltKeyGetter);
        accessor(instance, "ctrlKey", eventCtrlKeyGetter);
        accessor(instance, "shiftKey", eventShiftKeyGetter);
        accessor(instance, "metaKey", eventMetaKeyGetter);
        break;
    case Interface::WheelEvent:
        tmpl->SetClassName(toV8(isolate_, "WheelEvent"));
        tmpl->Inherit(templateFor(Interface::MouseEvent));
        accessor(instance, "deltaX", eventDeltaXGetter);
        accessor(instance, "deltaY", eventDeltaYGetter);
        accessor(instance, "deltaMode", eventDeltaModeGetter);
        break;
    case Interface::KeyboardEvent:
        tmpl->SetClassName(toV8(isolate_, "KeyboardEvent"));
        tmpl->Inherit(templateFor(Interface::Event));
        accessor(instance, "key", eventKeyGetter);
        accessor(instance, "code", eventCodeGetter);
        accessor(instance, "repeat", eventRepeatGetter);
        accessor(instance, "altKey", eventAltKeyGetter);
        accessor(instance, "ctrlKey", eventCtrlKeyGetter);
        accessor(instance, "shiftKey", eventShiftKeyGetter);
        accessor(instance, "metaKey", eventMetaKeyGetter);
        break;
    case Interface::InputEvent:
        tmpl->SetClassName(toV8(isolate_, "InputEvent"));
        tmpl->Inherit(templateFor(Interface::Event));
        accessor(instance, "data", eventDataGetter);
        accessor(instance, "inputType", eventInputTypeGetter);
        break;
    case Interface::FocusEvent:
        tmpl->SetClassName(toV8(isolate_, "FocusEvent"));
        tmpl->Inherit(templateFor(Interface::Event));
        accessor(instance, "relatedTarget", eventRelatedTargetGetter);
        break;
    case Interface::Document:
        tmpl->SetClassName(toV8(isolate_, "Document"));
        tmpl->Inherit(templateFor(Interface::Node));
        accessor(instance, "documentElement", documentElementGetter);
        accessor(instance, "head", documentHeadGetter);
        accessor(instance, "body", documentBodyGetter);
        accessor(instance, "title", documentTitleGetter);
        accessor(instance, "URL", documentUrlGetter);
        accessor(instance, "activeElement", activeElementGetter);
        method(proto, "getElementById", getElementByIdCallback);
        method(proto, "getElementsByClassName", getElementsByClassNameCallback);
        method(proto, "createElement", createElementCallback);
        method(proto, "createTextNode", createTextNodeCallback);
        method(proto, "createComment", createCommentCallback);
        break;
    case Interface::TokenList:
        tmpl->SetClassName(toV8(isolate_, "DOMTokenList"));
        accessor(instance, "length", tokenListLengthGetter);
        accessor(instance, "value", tokenListValueGetter);
        method(proto, "add", tokenListAddCallback);
        method(proto, "remove", tokenListRemoveCallback);
        method(proto, "toggle", tokenListToggleCallback);
        method(proto, "contains", tokenListContainsCallback);
        method(proto, "item", tokenListItemCallback);
        break;
    case Interface::StyleDeclaration:
        tmpl->SetClassName(toV8(isolate_, "CSSStyleDeclaration"));
        accessor(instance, "cssText", styleCssTextGetter);
        method(proto, "getPropertyValue", styleGetPropertyValueCallback);
        method(proto, "setProperty", styleSetPropertyCallback);
        method(proto, "removeProperty", styleRemovePropertyCallback);
        {
            v8::NamedPropertyHandlerConfiguration handler(styleNamedGetter, styleNamedSetter);
            handler.flags = v8::PropertyHandlerFlags::kNonMasking;
            instance->SetHandler(handler);
        }
        break;
    case Interface::Count:
        break;
    }
    return scope.Escape(tmpl);
}

v8::Local<v8::Value> DomBindings::wrap(v8::Local<v8::Context> context, dom::Node* node) {
    v8::EscapableHandleScope scope(isolate_);
    if (!node) {
        return scope.Escape(v8::Null(isolate_).As<v8::Value>());
    }
    if (auto* existing = static_cast<WrapperData*>(node->wrapperSlot().data)) {
        if (!existing->handle.IsEmpty()) {
            return scope.Escape(existing->handle.Get(isolate_).As<v8::Value>());
        }
    }

    Interface interface = Interface::Node;
    switch (node->nodeType()) {
    case dom::NodeType::Element:
        interface = Interface::Element;
        break;
    case dom::NodeType::Text:
        interface = Interface::Text;
        break;
    case dom::NodeType::Comment:
        interface = Interface::Comment;
        break;
    case dom::NodeType::Document:
        interface = Interface::Document;
        break;
    case dom::NodeType::DocumentFragment:
        interface = Interface::Node;
        break;
    }

    v8::Local<v8::Object> wrapper;
    if (!templateFor(interface)->InstanceTemplate()->NewInstance(context).ToLocal(&wrapper)) {
        return scope.Escape(v8::Null(isolate_).As<v8::Value>());
    }
    wrapper->SetAlignedPointerInInternalField(kNodePointerField, node);
    wrapper->SetAlignedPointerInInternalField(kTypeTagField, reinterpret_cast<void*>(kTagNode));

    auto* data = new WrapperData();
    data->node = node; // the wrapper keeps the node alive
    data->handle.Reset(isolate_, wrapper);
    data->handle.SetWeak(data, firstPassWeakCallback, v8::WeakCallbackType::kParameter);

    node->wrapperSlot().clear();
    node->wrapperSlot().data = data;
    node->wrapperSlot().destroy = &destroyWrapperData;
    return scope.Escape(wrapper.As<v8::Value>());
}

v8::Local<v8::Value> DomBindings::wrapTokenList(v8::Local<v8::Context> context, dom::Element& element) {
    v8::EscapableHandleScope scope(isolate_);
    v8::Local<v8::Object> list;
    if (!templateFor(Interface::TokenList)->InstanceTemplate()->NewInstance(context).ToLocal(&list)) {
        return scope.Escape(v8::Null(isolate_).As<v8::Value>());
    }
    // The list is a transient view over the element; the element outlives it
    // because JavaScript reached it through its own (ref-holding) wrapper.
    list->SetAlignedPointerInInternalField(kNodePointerField, &element);
    list->SetAlignedPointerInInternalField(kTypeTagField, reinterpret_cast<void*>(kTagElementView));
    return scope.Escape(list.As<v8::Value>());
}

v8::Local<v8::Value> DomBindings::wrapStyleDeclaration(v8::Local<v8::Context> context, dom::Element& element) {
    v8::EscapableHandleScope scope(isolate_);
    v8::Local<v8::Object> declaration;
    if (!templateFor(Interface::StyleDeclaration)->InstanceTemplate()->NewInstance(context).ToLocal(&declaration)) {
        return scope.Escape(v8::Null(isolate_).As<v8::Value>());
    }
    // Like the token list, this is a transient view over the element.
    declaration->SetAlignedPointerInInternalField(kNodePointerField, &element);
    declaration->SetAlignedPointerInInternalField(kTypeTagField, reinterpret_cast<void*>(kTagElementView));
    return scope.Escape(declaration.As<v8::Value>());
}

dom::Node* DomBindings::unwrap(v8::Local<v8::Value> value) {
    if (typeTagOf(value) != kTagNode) {
        return nullptr;
    }
    return static_cast<dom::Node*>(value.As<v8::Object>()->GetAlignedPointerFromInternalField(kNodePointerField));
}

dom::Element* DomBindings::unwrapView(v8::Local<v8::Value> value) {
    if (typeTagOf(value) != kTagElementView) {
        return nullptr;
    }
    return static_cast<dom::Element*>(value.As<v8::Object>()->GetAlignedPointerFromInternalField(kNodePointerField));
}

dom::Event* DomBindings::unwrapEvent(v8::Local<v8::Value> value) {
    if (typeTagOf(value) != kTagEvent) {
        return nullptr;
    }
    return static_cast<dom::Event*>(value.As<v8::Object>()->GetAlignedPointerFromInternalField(kNodePointerField));
}

v8::Local<v8::Value> DomBindings::wrapEvent(v8::Local<v8::Context> context, dom::Event* event) {
    v8::EscapableHandleScope scope(isolate_);
    if (!event) {
        return scope.Escape(v8::Null(isolate_).As<v8::Value>());
    }
    if (auto* existing = static_cast<EventWrapperData*>(event->wrapperSlot().data)) {
        if (!existing->handle.IsEmpty()) {
            return scope.Escape(existing->handle.Get(isolate_).As<v8::Value>());
        }
    }

    Interface interface = Interface::Event;
    switch (event->category()) {
    case dom::EventCategory::Mouse:
        interface = Interface::MouseEvent;
        break;
    case dom::EventCategory::Wheel:
        interface = Interface::WheelEvent;
        break;
    case dom::EventCategory::Keyboard:
        interface = Interface::KeyboardEvent;
        break;
    case dom::EventCategory::Input:
        interface = Interface::InputEvent;
        break;
    case dom::EventCategory::Focus:
        interface = Interface::FocusEvent;
        break;
    case dom::EventCategory::Plain:
        break;
    }

    v8::Local<v8::Object> wrapper;
    if (!templateFor(interface)->InstanceTemplate()->NewInstance(context).ToLocal(&wrapper)) {
        return scope.Escape(v8::Null(isolate_).As<v8::Value>());
    }
    wrapper->SetAlignedPointerInInternalField(kNodePointerField, event);
    wrapper->SetAlignedPointerInInternalField(kTypeTagField, reinterpret_cast<void*>(kTagEvent));

    auto* data = new EventWrapperData();
    data->event = event; // the wrapper keeps the event alive
    data->handle.Reset(isolate_, wrapper);
    data->handle.SetWeak(data, eventFirstPassWeakCallback, v8::WeakCallbackType::kParameter);

    event->wrapperSlot().clear();
    event->wrapperSlot().data = data;
    event->wrapperSlot().destroy = &destroyEventWrapperData;
    return scope.Escape(wrapper.As<v8::Value>());
}

namespace {

// Defined here because it needs DomBindings::wrapEvent.
void JsEventListener::handleEvent(dom::Event& event) {
    V8Runtime* runtime = V8Runtime::fromIsolate(isolate_);
    DomBindings* bindings = runtime ? runtime->domBindings() : nullptr;
    if (!runtime || !bindings || function_.IsEmpty()) {
        return;
    }
    v8::HandleScope scope(isolate_);
    v8::Local<v8::Context> context = runtime->context();
    if (context.IsEmpty()) {
        return;
    }
    v8::Context::Scope contextScope(context);
    v8::Local<v8::Value> argv[1] = {bindings->wrapEvent(context, &event)};
    // `this` is the node the listener is registered on, as the DOM specifies.
    v8::Local<v8::Value> thisValue = wrapNode(isolate_, event.currentTarget());
    runtime->callFunction(function_.Get(isolate_), thisValue, 1, argv);
}

} // namespace

void DomBindings::install(v8::Local<v8::Context> context, dom::Document& document) {
    v8::HandleScope scope(isolate_);
    document_ = &document;
    v8::Local<v8::Object> global = context->Global();
    v8::Local<v8::Value> wrapper = wrap(context, &document);
    global->Set(context, toV8(isolate_, "document"), wrapper).Check();

    // `window` is the global object, and it shares the document's event target:
    // a listener added on window is reached by the same dispatch path.
    const auto installFunction = [&](const char* name, v8::FunctionCallback callback) {
        v8::Local<v8::Function> function;
        if (v8::FunctionTemplate::New(isolate_, callback)->GetFunction(context).ToLocal(&function)) {
            global->Set(context, toV8(isolate_, name), function).Check();
        }
    };
    installFunction("addEventListener", addEventListenerCallback);
    installFunction("removeEventListener", removeEventListenerCallback);
    installFunction("dispatchEvent", dispatchEventCallback);

    // Constructors, so `instanceof` and `new Event(...)` work in page code.
    const auto installConstructor = [&](const char* name, Interface interface) {
        v8::Local<v8::Function> constructor;
        if (templateFor(interface)->GetFunction(context).ToLocal(&constructor)) {
            global->Set(context, toV8(isolate_, name), constructor).Check();
        }
    };
    installConstructor("EventTarget", Interface::EventTarget);
    installConstructor("Node", Interface::Node);
    installConstructor("Element", Interface::Element);
    installConstructor("Event", Interface::Event);
    installConstructor("MouseEvent", Interface::MouseEvent);
    installConstructor("WheelEvent", Interface::WheelEvent);
    installConstructor("KeyboardEvent", Interface::KeyboardEvent);
    installConstructor("InputEvent", Interface::InputEvent);
    installConstructor("FocusEvent", Interface::FocusEvent);
}

} // namespace xgu::js
