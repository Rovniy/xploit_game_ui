#include "css/Selector.h"
#include "css/SelectorMatcher.h"
#include "dom/Document.h"
#include "dom/Element.h"
#include "html/LexborHtmlParser.h"

#include <gtest/gtest.h>

using namespace xgu;
using namespace xgu::css;

namespace {

RefPtr<dom::Document> parse(std::string_view html) {
    RefPtr<dom::Document> document = makeRef<dom::Document>();
    html::LexborHtmlParser parser;
    EXPECT_TRUE(parser.parseDocument(html, *document));
    return document;
}

SelectorList parseSelector(std::string_view text) {
    std::string error;
    auto list = SelectorList::parse(text, &error);
    EXPECT_TRUE(list.has_value()) << text << ": " << error;
    return list.value_or(SelectorList{});
}

std::vector<std::string> idsOf(const std::vector<dom::Element*>& elements) {
    std::vector<std::string> ids;
    for (dom::Element* element : elements) {
        ids.push_back(element->id().string());
    }
    return ids;
}

} // namespace

TEST(Selector, ParsesTheSupportedForms) {
    EXPECT_TRUE(SelectorList::parse("div").has_value());
    EXPECT_TRUE(SelectorList::parse("*").has_value());
    EXPECT_TRUE(SelectorList::parse("#id").has_value());
    EXPECT_TRUE(SelectorList::parse(".cls").has_value());
    EXPECT_TRUE(SelectorList::parse("div#id.a.b").has_value());
    EXPECT_TRUE(SelectorList::parse("a b").has_value());
    EXPECT_TRUE(SelectorList::parse("a > b").has_value());
    EXPECT_TRUE(SelectorList::parse("a+b").has_value());
    EXPECT_TRUE(SelectorList::parse("a ~ b").has_value());
    EXPECT_TRUE(SelectorList::parse("a, b , c").has_value());
    EXPECT_TRUE(SelectorList::parse("[data-x]").has_value());
    EXPECT_TRUE(SelectorList::parse("[type=\"text\"]").has_value());
    EXPECT_TRUE(SelectorList::parse("input:focus").has_value());
    EXPECT_TRUE(SelectorList::parse("li:first-child").has_value());
    EXPECT_TRUE(SelectorList::parse("div:not(.skip)").has_value());
}

TEST(Selector, RejectsUnsupportedSyntax) {
    std::string error;
    EXPECT_FALSE(SelectorList::parse("", &error).has_value());
    EXPECT_FALSE(SelectorList::parse("div::before").has_value());
    EXPECT_FALSE(SelectorList::parse(":nth-child(2)").has_value());
    EXPECT_FALSE(SelectorList::parse("div >", nullptr).has_value());
    EXPECT_FALSE(SelectorList::parse("#", nullptr).has_value());
    EXPECT_FALSE(SelectorList::parse("[unclosed", nullptr).has_value());
}

TEST(Selector, Specificity) {
    const auto id = parseSelector("#a");
    const auto cls = parseSelector(".a");
    const auto type = parseSelector("div");
    const auto combined = parseSelector("div.a#b");
    EXPECT_GT(id.selectors[0].specificity, cls.selectors[0].specificity);
    EXPECT_GT(cls.selectors[0].specificity, type.selectors[0].specificity);
    EXPECT_EQ(combined.selectors[0].specificity, (1u << 20) | (1u << 10) | 1u);
    EXPECT_EQ(parseSelector("a b c").selectors[0].specificity, 3u);
}

TEST(Selector, MatchesSimpleCompounds) {
    RefPtr<dom::Document> document = parse(
        "<div id=\"root\" class=\"box big\" data-kind=\"panel\"><span id=\"child\">x</span></div>");
    SelectorMatcher matcher;
    dom::Element* root = document->getElementById("root");
    ASSERT_NE(root, nullptr);

    EXPECT_TRUE(matcher.matches(*root, parseSelector("div")));
    EXPECT_TRUE(matcher.matches(*root, parseSelector("#root")));
    EXPECT_TRUE(matcher.matches(*root, parseSelector(".box")));
    EXPECT_TRUE(matcher.matches(*root, parseSelector(".box.big")));
    EXPECT_TRUE(matcher.matches(*root, parseSelector("div#root.box")));
    EXPECT_TRUE(matcher.matches(*root, parseSelector("*")));
    EXPECT_FALSE(matcher.matches(*root, parseSelector("span")));
    EXPECT_FALSE(matcher.matches(*root, parseSelector(".missing")));
    EXPECT_TRUE(matcher.matches(*root, parseSelector("span, div")));
}

TEST(Selector, MatchesAttributeOperators) {
    RefPtr<dom::Document> document = parse("<a id=\"x\" href=\"https://example.com/page\" lang=\"en-US\" "
                                            "rel=\"noopener noreferrer\"></a>");
    SelectorMatcher matcher;
    dom::Element* a = document->getElementById("x");
    EXPECT_TRUE(matcher.matches(*a, parseSelector("[href]")));
    EXPECT_TRUE(matcher.matches(*a, parseSelector("[lang=\"en-US\"]")));
    EXPECT_TRUE(matcher.matches(*a, parseSelector("[lang|=en]")));
    EXPECT_TRUE(matcher.matches(*a, parseSelector("[rel~=noopener]")));
    EXPECT_TRUE(matcher.matches(*a, parseSelector("[href^=\"https\"]")));
    EXPECT_TRUE(matcher.matches(*a, parseSelector("[href$=\"page\"]")));
    EXPECT_TRUE(matcher.matches(*a, parseSelector("[href*=\"example\"]")));
    EXPECT_FALSE(matcher.matches(*a, parseSelector("[href^=\"http://\"]")));
    EXPECT_FALSE(matcher.matches(*a, parseSelector("[rel~=nope]")));
    EXPECT_TRUE(matcher.matches(*a, parseSelector("[lang=\"EN-us\" i]")));
}

TEST(Selector, MatchesCombinators) {
    RefPtr<dom::Document> document = parse("<div id=\"a\"><p id=\"b\"><span id=\"c\">x</span></p>"
                                            "<span id=\"d\"></span><span id=\"e\"></span></div>");
    SelectorMatcher matcher;
    EXPECT_TRUE(matcher.matches(*document->getElementById("c"), parseSelector("div span")));
    EXPECT_FALSE(matcher.matches(*document->getElementById("c"), parseSelector("div > span")));
    EXPECT_TRUE(matcher.matches(*document->getElementById("c"), parseSelector("p > span")));
    EXPECT_TRUE(matcher.matches(*document->getElementById("d"), parseSelector("p + span")));
    EXPECT_FALSE(matcher.matches(*document->getElementById("e"), parseSelector("p + span")));
    EXPECT_TRUE(matcher.matches(*document->getElementById("e"), parseSelector("p ~ span")));
    EXPECT_TRUE(matcher.matches(*document->getElementById("c"), parseSelector("div#a > p#b > span#c")));
}

TEST(Selector, MatchesStructuralPseudoClasses) {
    RefPtr<dom::Document> document =
        parse("<ul id=\"list\"><li id=\"one\">1</li><li id=\"two\">2</li><li id=\"three\"></li></ul>");
    SelectorMatcher matcher;
    EXPECT_TRUE(matcher.matches(*document->getElementById("one"), parseSelector("li:first-child")));
    EXPECT_FALSE(matcher.matches(*document->getElementById("two"), parseSelector("li:first-child")));
    EXPECT_TRUE(matcher.matches(*document->getElementById("three"), parseSelector("li:last-child")));
    EXPECT_TRUE(matcher.matches(*document->getElementById("three"), parseSelector("li:empty")));
    EXPECT_FALSE(matcher.matches(*document->getElementById("one"), parseSelector("li:empty")));
    EXPECT_TRUE(matcher.matches(*document->getElementById("two"), parseSelector("li:not(:first-child)")));
    EXPECT_FALSE(matcher.matches(*document->getElementById("one"), parseSelector("li:not(:first-child)")));
    ASSERT_NE(document->documentElement(), nullptr);
    EXPECT_TRUE(matcher.matches(*document->documentElement(), parseSelector(":root")));
}

TEST(Selector, PseudoClassesWithoutStateAreFalse) {
    RefPtr<dom::Document> document = parse("<button id=\"b\" disabled></button>");
    SelectorMatcher matcher;
    dom::Element* button = document->getElementById("b");
    EXPECT_FALSE(matcher.matches(*button, parseSelector(":hover")));
    EXPECT_FALSE(matcher.matches(*button, parseSelector(":focus")));
    EXPECT_TRUE(matcher.matches(*button, parseSelector(":disabled")));
    EXPECT_FALSE(matcher.matches(*button, parseSelector(":enabled")));
}

TEST(Selector, QueryFirstAndAllUseTreeOrder) {
    RefPtr<dom::Document> document =
        parse("<div id=\"a\" class=\"c\"><span id=\"b\" class=\"c\"></span></div><span id=\"d\" class=\"c\"></span>");
    SelectorMatcher matcher;
    const SelectorList byClass = parseSelector(".c");
    dom::Element* first = matcher.queryFirst(*document, byClass);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->id().string(), "a");
    EXPECT_EQ(idsOf(matcher.queryAll(*document, byClass)), (std::vector<std::string>{"a", "b", "d"}));

    // Scoped to a subtree: the root itself is not considered.
    dom::Element* a = document->getElementById("a");
    EXPECT_EQ(idsOf(matcher.queryAll(*a, byClass)), (std::vector<std::string>{"b"}));
    EXPECT_EQ(matcher.queryFirst(*a, parseSelector("#a")), nullptr);
}
