#pragma once

#include "css/ComputedStyle.h"
#include "css/StyleSheet.h"
#include "dom/Node.h"
#include "html/HtmlTags.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xgu::dom {

struct Attribute {
    Atom name; // lowercase
    std::string value;
};

class Element : public Node {
public:
    Element(Document* document, Atom tagName);

    const Atom& tagName() const { return tagName_; }
    html::HtmlTag knownTag() const { return knownTag_; }
    // Uppercase, as the DOM reports it for HTML elements.
    std::string nodeName() const override;

    const Atom& id() const { return id_; }
    const std::vector<Atom>& classList() const { return classes_; }
    bool hasClass(const Atom& name) const;
    std::string className() const;
    void setClassName(std::string_view value);
    bool addClass(std::string_view name);
    bool removeClass(std::string_view name);
    bool toggleClass(std::string_view name);

    const std::vector<Attribute>& attributes() const { return attributes_; }
    bool hasAttribute(const Atom& name) const;
    const std::string* getAttribute(const Atom& name) const;
    std::string getAttributeOrEmpty(const Atom& name) const;
    void setAttribute(const Atom& name, std::string_view value);
    bool removeAttribute(const Atom& name);

    // Element children only, in tree order.
    std::vector<Element*> childElements() const;

    // --- style ---------------------------------------------------------------
    // Declarations of the style="" attribute (null when there are none).
    const css::DeclarationBlock* inlineStyle() const { return inlineStyle_.get(); }
    css::DeclarationBlock& ensureInlineStyle();
    // Rewrites the style="" attribute from the current declarations.
    void syncInlineStyleAttribute();

    // Filled by css::StyleEngine; null until the first style recalculation.
    const css::ComputedStyle* computedStyle() const { return computedStyle_.get(); }
    void setComputedStyle(RefPtr<css::ComputedStyle> style) { computedStyle_ = std::move(style); }

    std::string innerHTML() const;
    std::string outerHTML() const;
    // Replaces the children with the parsed fragment. Returns false on a parse error.
    bool setInnerHTML(std::string_view html);

    // Closest ancestor-or-self matching the predicate; used by the event and
    // selector code.
    template <typename Predicate>
    Element* closest(Predicate predicate) {
        for (Element* element = this; element; element = element->parentElement()) {
            if (predicate(*element)) {
                return element;
            }
        }
        return nullptr;
    }

protected:
    void didConnect() override;
    void didDisconnect() override;

private:
    void updateIdFromAttribute(std::string_view value);
    void updateClassesFromAttribute(std::string_view value);
    void updateInlineStyleFromAttribute(std::string_view value);

    Atom tagName_;
    html::HtmlTag knownTag_;
    Atom id_;
    std::vector<Atom> classes_;
    std::vector<Attribute> attributes_;
    std::unique_ptr<css::DeclarationBlock> inlineStyle_;
    RefPtr<css::ComputedStyle> computedStyle_;
    bool syncingStyleAttribute_ = false;
};

} // namespace xgu::dom
