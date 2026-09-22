#include "dom/Serializer.h"

#include "dom/Document.h"
#include "dom/Element.h"
#include "dom/Node.h"

namespace xgu::dom {
namespace {

void appendEscaped(std::string& out, std::string_view text, bool attributeMode) {
    for (char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            if (attributeMode) {
                out.push_back(c);
            } else {
                out += "&lt;";
            }
            break;
        case '>':
            if (attributeMode) {
                out.push_back(c);
            } else {
                out += "&gt;";
            }
            break;
        case '"':
            if (attributeMode) {
                out += "&quot;";
            } else {
                out.push_back(c);
            }
            break;
        case '\xA0': // non-breaking space
            out += "&nbsp;";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
}

void serializeInto(std::string& out, const Node& node);

void serializeChildrenInto(std::string& out, const Node& node) {
    for (size_t i = 0; i < node.childCount(); ++i) {
        serializeInto(out, *node.childAt(i));
    }
}

void serializeInto(std::string& out, const Node& node) {
    switch (node.nodeType()) {
    case NodeType::Text: {
        const auto& text = static_cast<const CharacterData&>(node);
        const Element* parent = node.parentElement();
        if (parent && html::isRawTextTag(parent->knownTag())) {
            out += text.data(); // script/style content is not escaped
        } else {
            appendEscaped(out, text.data(), false);
        }
        break;
    }
    case NodeType::Comment:
        out += "<!--";
        out += static_cast<const CharacterData&>(node).data();
        out += "-->";
        break;
    case NodeType::Element: {
        const auto& element = static_cast<const Element&>(node);
        out.push_back('<');
        out += element.tagName().string();
        for (const Attribute& attribute : element.attributes()) {
            out.push_back(' ');
            out += attribute.name.string();
            out += "=\"";
            appendEscaped(out, attribute.value, true);
            out.push_back('"');
        }
        out.push_back('>');
        if (html::isVoidTag(element.knownTag())) {
            break;
        }
        serializeChildrenInto(out, element);
        out += "</";
        out += element.tagName().string();
        out.push_back('>');
        break;
    }
    case NodeType::Document:
    case NodeType::DocumentFragment:
        serializeChildrenInto(out, node);
        break;
    }
}

} // namespace

std::string serializeNode(const Node& node) {
    std::string out;
    serializeInto(out, node);
    return out;
}

std::string serializeChildren(const Node& node) {
    std::string out;
    serializeChildrenInto(out, node);
    return out;
}

std::string escapeHtmlText(std::string_view text) {
    std::string out;
    appendEscaped(out, text, false);
    return out;
}

std::string escapeHtmlAttribute(std::string_view value) {
    std::string out;
    appendEscaped(out, value, true);
    return out;
}

} // namespace xgu::dom
