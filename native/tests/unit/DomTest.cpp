#include "dom/Document.h"
#include "dom/Element.h"
#include "dom/Node.h"
#include "dom/Serializer.h"

#include <gtest/gtest.h>

using namespace xgu;
using namespace xgu::dom;

namespace {

RefPtr<Document> makeDocument() { return makeRef<Document>(); }

// <html><body><div id="a" class="x y"><span>text</span></div></body></html>
RefPtr<Document> makeTree(Element** outDiv = nullptr) {
    RefPtr<Document> document = makeDocument();
    RefPtr<Element> html = document->createElement("html");
    RefPtr<Element> body = document->createElement("body");
    RefPtr<Element> div = document->createElement("div");
    RefPtr<Element> span = document->createElement("span");
    div->setAttribute(Atom("id"), "a");
    div->setAttribute(Atom("class"), "x y");
    span->appendChild(*document->createTextNode("text"));
    div->appendChild(*span);
    body->appendChild(*div);
    html->appendChild(*body);
    document->appendChild(*html);
    document->updateWellKnownElements();
    if (outDiv) {
        *outDiv = div.get();
    }
    return document;
}

} // namespace

TEST(Dom, TreeStructureAndSiblings) {
    RefPtr<Document> document = makeDocument();
    RefPtr<Element> parent = document->createElement("div");
    RefPtr<Element> a = document->createElement("span");
    RefPtr<Element> b = document->createElement("span");
    RefPtr<Element> c = document->createElement("span");
    document->appendChild(*parent);
    parent->appendChild(*a);
    parent->appendChild(*c);
    parent->insertBefore(*b, c.get());

    EXPECT_EQ(parent->childCount(), 3u);
    EXPECT_EQ(parent->childAt(0), a.get());
    EXPECT_EQ(parent->childAt(1), b.get());
    EXPECT_EQ(parent->childAt(2), c.get());
    EXPECT_EQ(a->nextSibling(), b.get());
    EXPECT_EQ(b->previousSibling(), a.get());
    EXPECT_EQ(c->nextSibling(), nullptr);
    EXPECT_EQ(a->previousSibling(), nullptr);
    EXPECT_EQ(parent->firstChild(), a.get());
    EXPECT_EQ(parent->lastChild(), c.get());
    EXPECT_EQ(b->parentNode(), parent.get());
    EXPECT_TRUE(parent->contains(*b));
    EXPECT_FALSE(b->contains(*parent));
}

TEST(Dom, MovingANodeDetachesItFromTheOldParent) {
    RefPtr<Document> document = makeDocument();
    RefPtr<Element> first = document->createElement("div");
    RefPtr<Element> second = document->createElement("div");
    RefPtr<Element> child = document->createElement("span");
    document->appendChild(*first);
    document->appendChild(*second);
    first->appendChild(*child);
    EXPECT_EQ(first->childCount(), 1u);

    second->appendChild(*child);
    EXPECT_EQ(first->childCount(), 0u);
    EXPECT_EQ(second->childCount(), 1u);
    EXPECT_EQ(child->parentNode(), second.get());
}

TEST(Dom, CyclesAreRejected) {
    RefPtr<Document> document = makeDocument();
    RefPtr<Element> parent = document->createElement("div");
    RefPtr<Element> child = document->createElement("div");
    parent->appendChild(*child);
    EXPECT_FALSE(child->appendChild(*parent));
    EXPECT_FALSE(parent->appendChild(*parent));
}

TEST(Dom, RemovedNodesStayAliveAndDisconnect) {
    RefPtr<Document> document = makeDocument();
    RefPtr<Element> parent = document->createElement("div");
    RefPtr<Element> child = document->createElement("span");
    document->appendChild(*parent);
    parent->appendChild(*child);
    EXPECT_TRUE(child->isConnected());

    parent->removeChild(*child);
    EXPECT_FALSE(child->isConnected());
    EXPECT_EQ(child->parentNode(), nullptr);
    EXPECT_EQ(child->nodeName(), "SPAN"); // still usable
}

TEST(Dom, TextContentReadsAndWrites) {
    Element* div = nullptr;
    RefPtr<Document> document = makeTree(&div);
    EXPECT_EQ(div->textContent(), "text");
    EXPECT_EQ(document->body()->textContent(), "text");

    div->setTextContent("replaced");
    EXPECT_EQ(div->childCount(), 1u);
    EXPECT_TRUE(div->firstChild()->isText());
    EXPECT_EQ(div->textContent(), "replaced");

    div->setTextContent("");
    EXPECT_EQ(div->childCount(), 0u);
    EXPECT_EQ(div->textContent(), "");
}

TEST(Dom, IdMapTracksConnectedElements) {
    Element* div = nullptr;
    RefPtr<Document> document = makeTree(&div);
    EXPECT_EQ(document->getElementById("a"), div);
    EXPECT_EQ(document->getElementById("missing"), nullptr);

    div->setAttribute(Atom("id"), "b");
    EXPECT_EQ(document->getElementById("a"), nullptr);
    EXPECT_EQ(document->getElementById("b"), div);

    RefPtr<Node> protector(div);
    div->remove();
    EXPECT_EQ(document->getElementById("b"), nullptr);

    document->body()->appendChild(*div);
    EXPECT_EQ(document->getElementById("b"), div);
}

TEST(Dom, ClassListOperations) {
    Element* div = nullptr;
    RefPtr<Document> document = makeTree(&div);
    EXPECT_TRUE(div->hasClass(Atom("x")));
    EXPECT_TRUE(div->hasClass(Atom("y")));
    EXPECT_FALSE(div->hasClass(Atom("z")));

    EXPECT_TRUE(div->addClass("z"));
    EXPECT_FALSE(div->addClass("z"));
    EXPECT_TRUE(div->hasClass(Atom("z")));
    EXPECT_TRUE(div->removeClass("x"));
    EXPECT_FALSE(div->hasClass(Atom("x")));
    EXPECT_TRUE(div->toggleClass("w"));
    EXPECT_FALSE(div->toggleClass("w"));
    EXPECT_EQ(div->classList().size(), 2u); // y, z
}

TEST(Dom, AttributesRoundTrip) {
    RefPtr<Document> document = makeDocument();
    RefPtr<Element> input = document->createElement("input");
    EXPECT_FALSE(input->hasAttribute(Atom("type")));
    input->setAttribute(Atom("type"), "text");
    EXPECT_TRUE(input->hasAttribute(Atom("type")));
    EXPECT_EQ(input->getAttributeOrEmpty(Atom("type")), "text");
    input->setAttribute(Atom("type"), "password");
    EXPECT_EQ(input->getAttributeOrEmpty(Atom("type")), "password");
    EXPECT_TRUE(input->removeAttribute(Atom("type")));
    EXPECT_FALSE(input->removeAttribute(Atom("type")));
    EXPECT_FALSE(input->hasAttribute(Atom("type")));
}

TEST(Dom, ElementNamesAreUppercaseTagsLowercase) {
    RefPtr<Document> document = makeDocument();
    RefPtr<Element> div = document->createElement("DiV");
    EXPECT_EQ(div->tagName().string(), "div");
    EXPECT_EQ(div->nodeName(), "DIV");
    EXPECT_EQ(div->knownTag(), html::HtmlTag::Div);
    RefPtr<Element> custom = document->createElement("my-widget");
    EXPECT_EQ(custom->knownTag(), html::HtmlTag::Unknown);
}

TEST(Dom, SerializerEscapesAndHandlesVoidElements) {
    RefPtr<Document> document = makeDocument();
    RefPtr<Element> div = document->createElement("div");
    document->appendChild(*div);
    div->setAttribute(Atom("title"), "a \"quoted\" & <value>");
    div->appendChild(*document->createTextNode("5 < 6 & 7 > 3"));
    div->appendChild(*document->createElement("br"));
    div->appendChild(*document->createComment("note"));

    const std::string html = div->outerHTML();
    EXPECT_NE(html.find("title=\"a &quot;quoted&quot; &amp; <value>\""), std::string::npos) << html;
    EXPECT_NE(html.find("5 &lt; 6 &amp; 7 &gt; 3"), std::string::npos) << html;
    EXPECT_NE(html.find("<br>"), std::string::npos) << html;
    EXPECT_EQ(html.find("</br>"), std::string::npos) << html;
    EXPECT_NE(html.find("<!--note-->"), std::string::npos) << html;
}

TEST(Dom, DirtyBitsPropagateToAncestors) {
    Element* div = nullptr;
    RefPtr<Document> document = makeTree(&div);
    for (dom::Node* node = document.get(); node; node = dom::nextInTreeOrder(node, document.get())) {
        node->clearDirty(0xFF);
    }

    div->markDirty(kDirtyPaintSelf);
    EXPECT_TRUE(div->dirtyBits() & kDirtyPaintSelf);
    EXPECT_FALSE(document->body()->dirtyBits() & kDirtyPaintChildren) << "paint-self does not propagate";

    div->markDirty(kDirtyStyleChildren);
    EXPECT_TRUE(document->body()->dirtyBits() & kDirtyStyleChildren);
    EXPECT_TRUE(document->dirtyBits() & kDirtyStyleChildren);
}

TEST(Dom, GetElementsByTagAndClass) {
    Element* div = nullptr;
    RefPtr<Document> document = makeTree(&div);
    EXPECT_EQ(document->getElementsByTagName("span").size(), 1u);
    EXPECT_EQ(document->getElementsByTagName("SPAN").size(), 1u);
    EXPECT_EQ(document->getElementsByTagName("div").size(), 1u);
    EXPECT_EQ(document->getElementsByClassName("x").size(), 1u);
    EXPECT_EQ(document->getElementsByClassName("nope").size(), 0u);
}
