#pragma once

#include "dom/Node.h"
#include "html/HtmlTags.h"

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

    Atom tagName_;
    html::HtmlTag knownTag_;
    Atom id_;
    std::vector<Atom> classes_;
    std::vector<Attribute> attributes_;
};

} // namespace xgu::dom
