#include "html/LexborHtmlParser.h"

#include "core/Log.h"
#include "dom/Document.h"
#include "dom/Element.h"
#include "dom/Node.h"

#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>

#include <string>

namespace xgu::html {
namespace {

std::string_view toView(const lxb_char_t* data, size_t length) {
    return data ? std::string_view(reinterpret_cast<const char*>(data), length) : std::string_view{};
}

// Converts one lexbor node (and its subtree) into engine nodes appended to `parent`.
void convertNode(lxb_dom_node_t* source, dom::Node& parent, dom::Document& document) {
    switch (source->type) {
    case LXB_DOM_NODE_TYPE_ELEMENT: {
        lxb_dom_element_t* sourceElement = lxb_dom_interface_element(source);
        size_t nameLength = 0;
        const lxb_char_t* name = lxb_dom_element_qualified_name(sourceElement, &nameLength);
        RefPtr<dom::Element> element = document.createElement(toView(name, nameLength));
        for (lxb_dom_attr_t* attribute = lxb_dom_element_first_attribute_noi(sourceElement); attribute != nullptr;
             attribute = lxb_dom_element_next_attribute_noi(attribute)) {
            size_t attrNameLength = 0;
            const lxb_char_t* attrName = lxb_dom_attr_qualified_name(attribute, &attrNameLength);
            size_t valueLength = 0;
            const lxb_char_t* value = lxb_dom_attr_value_noi(attribute, &valueLength);
            element->setAttribute(Atom::lowered(toView(attrName, attrNameLength)), toView(value, valueLength));
        }
        parent.appendChild(*element);
        for (lxb_dom_node_t* child = source->first_child; child != nullptr; child = child->next) {
            convertNode(child, *element, document);
        }
        break;
    }
    case LXB_DOM_NODE_TYPE_TEXT: {
        const lexbor_str_t& data = lxb_dom_interface_text(source)->char_data.data;
        RefPtr<dom::Text> text = document.createTextNode(toView(data.data, data.length));
        parent.appendChild(*text);
        break;
    }
    case LXB_DOM_NODE_TYPE_COMMENT: {
        const lexbor_str_t& data = lxb_dom_interface_comment(source)->char_data.data;
        RefPtr<dom::Comment> comment = document.createComment(toView(data.data, data.length));
        parent.appendChild(*comment);
        break;
    }
    default:
        // Doctype, processing instructions and the rest are not represented.
        break;
    }
}

// RAII wrapper so early returns cannot leak the lexbor document.
struct LexborDocument {
    lxb_html_document_t* handle = lxb_html_document_create();
    ~LexborDocument() {
        if (handle) {
            lxb_html_document_destroy(handle);
        }
    }
    explicit operator bool() const { return handle != nullptr; }
};

} // namespace

bool LexborHtmlParser::parseDocument(std::string_view html, dom::Document& document) {
    LexborDocument source;
    if (!source) {
        XGU_LOG_ERROR("html parser: lxb_html_document_create failed");
        return false;
    }
    const lxb_status_t status = lxb_html_document_parse(
        source.handle, reinterpret_cast<const lxb_char_t*>(html.data()), html.size());
    if (status != LXB_STATUS_OK) {
        XGU_LOG_ERROR("html parser: parse failed (lexbor status %d)", static_cast<int>(status));
        return false;
    }

    document.removeAllChildren();
    lxb_dom_node_t* root = lxb_dom_interface_node(source.handle);
    for (lxb_dom_node_t* child = root->first_child; child != nullptr; child = child->next) {
        convertNode(child, document, document);
    }
    document.updateWellKnownElements();
    return true;
}

RefPtr<dom::DocumentFragment> LexborHtmlParser::parseFragment(std::string_view html, dom::Element& contextElement,
                                                              dom::Document& document) {
    LexborDocument source;
    if (!source) {
        XGU_LOG_ERROR("html parser: lxb_html_document_create failed");
        return nullptr;
    }
    // The fragment parsing algorithm needs a context element of the same tag.
    const std::string& tagName = contextElement.tagName().string();
    lxb_dom_element_t* context = lxb_dom_document_create_element(
        lxb_dom_interface_document(source.handle), reinterpret_cast<const lxb_char_t*>(tagName.data()),
        tagName.size(), nullptr);
    if (!context) {
        XGU_LOG_ERROR("html parser: failed to create the fragment context element <%s>", tagName.c_str());
        return nullptr;
    }
    lxb_dom_node_t* parsed = lxb_html_document_parse_fragment(
        source.handle, context, reinterpret_cast<const lxb_char_t*>(html.data()), html.size());
    if (!parsed) {
        XGU_LOG_ERROR("html parser: fragment parse failed");
        return nullptr;
    }

    RefPtr<dom::DocumentFragment> fragment = document.createDocumentFragment();
    for (lxb_dom_node_t* child = parsed->first_child; child != nullptr; child = child->next) {
        convertNode(child, *fragment, document);
    }
    return fragment;
}

} // namespace xgu::html
