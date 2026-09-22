#include "dom/Document.h"
#include "dom/Element.h"
#include "dom/Serializer.h"
#include "html/LexborHtmlParser.h"

#include <gtest/gtest.h>

using namespace xgu;
using namespace xgu::dom;

namespace {

RefPtr<Document> parse(std::string_view html) {
    RefPtr<Document> document = makeRef<Document>();
    html::LexborHtmlParser parser;
    EXPECT_TRUE(parser.parseDocument(html, *document));
    return document;
}

} // namespace

TEST(HtmlParser, BuildsTheImpliedDocumentStructure) {
    RefPtr<Document> document = parse("<div id=\"a\">Hello</div>");
    ASSERT_NE(document->documentElement(), nullptr);
    EXPECT_EQ(document->documentElement()->tagName().string(), "html");
    ASSERT_NE(document->head(), nullptr);
    ASSERT_NE(document->body(), nullptr);

    Element* div = document->getElementById("a");
    ASSERT_NE(div, nullptr);
    EXPECT_EQ(div->tagName().string(), "div");
    EXPECT_EQ(div->textContent(), "Hello");
    EXPECT_EQ(div->parentNode(), document->body());
    EXPECT_TRUE(div->isConnected());
}

TEST(HtmlParser, ParsesAttributesLowercased) {
    RefPtr<Document> document = parse("<input TYPE=\"Text\" Placeholder='name' disabled>");
    Element* input = document->getElementsByTagName("input").at(0);
    EXPECT_EQ(input->getAttributeOrEmpty(Atom("type")), "Text") << "values keep their case";
    EXPECT_EQ(input->getAttributeOrEmpty(Atom("placeholder")), "name");
    EXPECT_TRUE(input->hasAttribute(Atom("disabled")));
}

TEST(HtmlParser, HandlesNestingTextAndComments) {
    RefPtr<Document> document = parse("<ul><li>one</li><li>two<!--c--></li></ul>");
    const auto items = document->getElementsByTagName("li");
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0]->textContent(), "one");
    EXPECT_EQ(items[1]->textContent(), "two");
    EXPECT_EQ(items[1]->childCount(), 2u); // text + comment
    EXPECT_TRUE(items[1]->lastChild()->isComment());
}

TEST(HtmlParser, RecoversFromMalformedMarkup) {
    RefPtr<Document> document = parse("<div><p>unclosed<div>next</div>");
    EXPECT_GE(document->getElementsByTagName("div").size(), 2u);
    EXPECT_EQ(document->getElementsByTagName("p").size(), 1u);
}

TEST(HtmlParser, EntitiesAreDecoded) {
    RefPtr<Document> document = parse("<div id=\"t\">5 &lt; 6 &amp;&amp; 7 &gt; 3</div>");
    EXPECT_EQ(document->getElementById("t")->textContent(), "5 < 6 && 7 > 3");
}

TEST(HtmlParser, UnicodeTextSurvivesRoundTrip) {
    RefPtr<Document> document = parse("<div id=\"t\">Привет, мир</div>");
    EXPECT_EQ(document->getElementById("t")->textContent(), "Привет, мир");
}

TEST(HtmlParser, ScriptContentIsRawText) {
    RefPtr<Document> document = parse("<script>if (a < b && c > d) { x(); }</script>");
    const auto scripts = document->getElementsByTagName("script");
    ASSERT_EQ(scripts.size(), 1u);
    EXPECT_EQ(scripts[0]->textContent(), "if (a < b && c > d) { x(); }");
    // ... and it serialises back unescaped.
    EXPECT_NE(scripts[0]->outerHTML().find("a < b && c > d"), std::string::npos);
}

TEST(HtmlParser, ReparsingReplacesTheOldTree) {
    RefPtr<Document> document = parse("<div id=\"first\"></div>");
    EXPECT_NE(document->getElementById("first"), nullptr);
    html::LexborHtmlParser parser;
    ASSERT_TRUE(parser.parseDocument("<div id=\"second\"></div>", *document));
    EXPECT_EQ(document->getElementById("first"), nullptr);
    EXPECT_NE(document->getElementById("second"), nullptr);
}

TEST(HtmlParser, InnerHtmlParsesFragments) {
    RefPtr<Document> document = parse("<div id=\"host\"></div>");
    Element* host = document->getElementById("host");
    ASSERT_TRUE(host->setInnerHTML("<span class=\"c\">a</span><b>b</b>"));
    EXPECT_EQ(host->childCount(), 2u);
    EXPECT_EQ(host->textContent(), "ab");
    EXPECT_EQ(document->getElementsByClassName("c").size(), 1u);
    EXPECT_EQ(host->innerHTML(), "<span class=\"c\">a</span><b>b</b>");
}

TEST(HtmlParser, InnerHtmlReplacesPreviousChildren) {
    RefPtr<Document> document = parse("<div id=\"host\"><em id=\"old\">x</em></div>");
    Element* host = document->getElementById("host");
    ASSERT_TRUE(host->setInnerHTML("<i>new</i>"));
    EXPECT_EQ(host->childCount(), 1u);
    EXPECT_EQ(document->getElementById("old"), nullptr) << "removed elements leave the id map";
}

TEST(HtmlParser, OuterHtmlRoundTrip) {
    RefPtr<Document> document = parse("<div id=\"a\" class=\"b\"><span>t</span></div>");
    Element* div = document->getElementById("a");
    EXPECT_EQ(div->outerHTML(), "<div id=\"a\" class=\"b\"><span>t</span></div>");
}
