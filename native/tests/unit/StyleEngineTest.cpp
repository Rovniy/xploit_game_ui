#include "css/StyleEngine.h"
#include "dom/Document.h"
#include "dom/Element.h"
#include "html/LexborHtmlParser.h"

#include <gtest/gtest.h>

#include <memory>

using namespace xgu;
using namespace xgu::css;

namespace {

class StyleEngineTest : public ::testing::Test {
protected:
    void load(std::string_view html, std::string_view css = {}) {
        document_ = makeRef<dom::Document>();
        html::LexborHtmlParser parser;
        ASSERT_TRUE(parser.parseDocument(html, *document_));
        engine_ = std::make_unique<StyleEngine>(*document_);
        if (!css.empty()) {
            engine_->addStyleSheet(css);
        }
        engine_->recalcStyles(800.0f, 600.0f);
    }

    const ComputedStyle* styleOf(std::string_view id) {
        dom::Element* element = document_->getElementById(id);
        return element ? element->computedStyle() : nullptr;
    }

    dom::Element* element(std::string_view id) { return document_->getElementById(id); }

    void recalc() { engine_->recalcStyles(800.0f, 600.0f); }

    RefPtr<dom::Document> document_;
    std::unique_ptr<StyleEngine> engine_;
};

} // namespace

TEST_F(StyleEngineTest, UserAgentDefaultsApply) {
    load("<div id=\"d\">x</div><span id=\"s\">y</span><button id=\"b\">z</button>");
    ASSERT_NE(styleOf("d"), nullptr);
    EXPECT_EQ(styleOf("d")->display, Display::Block);
    EXPECT_EQ(styleOf("s")->display, Display::Inline);
    EXPECT_EQ(styleOf("b")->display, Display::InlineBlock);
    EXPECT_EQ(styleOf("b")->boxSizing, BoxSizing::BorderBox);
    EXPECT_FLOAT_EQ(styleOf("b")->borderWidth[kTop], 1.0f);
    EXPECT_EQ(styleOf("b")->borderStyle[kTop], BorderStyle::Solid);
}

TEST_F(StyleEngineTest, AuthorRulesOverrideTheUserAgent) {
    load("<div id=\"d\">x</div>", "div { display: flex; background-color: #102030; }");
    EXPECT_EQ(styleOf("d")->display, Display::Flex);
    EXPECT_EQ(styleOf("d")->backgroundColor, Color::rgba(0x10, 0x20, 0x30));
}

TEST_F(StyleEngineTest, SpecificityDecidesTheWinner) {
    load("<div id=\"d\" class=\"a b\">x</div>",
         "div { color: #010101; }"
         ".a { color: #020202; }"
         "div.a { color: #030303; }"
         "#d { color: #040404; }");
    EXPECT_EQ(styleOf("d")->color, Color::rgba(4, 4, 4));
}

TEST_F(StyleEngineTest, SourceOrderBreaksTies) {
    load("<div id=\"d\" class=\"a\">x</div>", ".a { color: #010101; } .a { color: #020202; }");
    EXPECT_EQ(styleOf("d")->color, Color::rgba(2, 2, 2));
}

TEST_F(StyleEngineTest, ImportantBeatsHigherSpecificity) {
    load("<div id=\"d\" class=\"a\">x</div>", "#d { color: #010101; } .a { color: #020202 !important; }");
    EXPECT_EQ(styleOf("d")->color, Color::rgba(2, 2, 2));
}

TEST_F(StyleEngineTest, InlineStyleBeatsAuthorRules) {
    load("<div id=\"d\" style=\"color: #050505; width: 42px\">x</div>", "#d { color: #010101; width: 10px; }");
    EXPECT_EQ(styleOf("d")->color, Color::rgba(5, 5, 5));
    EXPECT_EQ(styleOf("d")->width, Length::px(42.0f));
}

TEST_F(StyleEngineTest, ImportantAuthorBeatsInline) {
    load("<div id=\"d\" style=\"color: #050505\">x</div>", "#d { color: #010101 !important; }");
    EXPECT_EQ(styleOf("d")->color, Color::rgba(1, 1, 1));
}

TEST_F(StyleEngineTest, InheritanceFlowsDown) {
    load("<div id=\"outer\"><div id=\"inner\">x</div></div>",
         "#outer { color: #123456; font-size: 20px; width: 500px; }");
    EXPECT_EQ(styleOf("inner")->color, Color::rgba(0x12, 0x34, 0x56)) << "color is inherited";
    EXPECT_FLOAT_EQ(styleOf("inner")->fontSize, 20.0f) << "font-size is inherited";
    EXPECT_TRUE(styleOf("inner")->width.isAuto()) << "width is not inherited";
}

TEST_F(StyleEngineTest, RelativeUnitsResolveAgainstTheRightFontSize) {
    load("<div id=\"outer\"><div id=\"inner\">x</div></div>",
         "html { font-size: 10px; }"
         "#outer { font-size: 2em; padding-top: 1em; }"   // 20px, padding stays 1em
         "#inner { font-size: 1.5em; margin-top: 2rem; }" // 30px, margin 2 * 10px
    );
    EXPECT_FLOAT_EQ(styleOf("outer")->fontSize, 20.0f);
    EXPECT_FLOAT_EQ(styleOf("inner")->fontSize, 30.0f);
    // em/rem lengths of box properties stay unresolved for layout, but border
    // widths and line heights are resolved during the cascade.
    EXPECT_EQ(styleOf("outer")->padding[kTop].unit, LengthUnit::Em);
    EXPECT_EQ(styleOf("inner")->margin[kTop].unit, LengthUnit::Rem);
}

TEST_F(StyleEngineTest, ViewportUnitsUseTheViewport) {
    load("<div id=\"d\">x</div>", "#d { border-top-width: 10vw; border-top-style: solid; }");
    EXPECT_FLOAT_EQ(styleOf("d")->borderWidth[kTop], 80.0f); // 10% of 800
}

TEST_F(StyleEngineTest, LineHeightNumberMultipliesFontSize) {
    load("<div id=\"d\">x</div>", "#d { font-size: 20px; line-height: 1.5; }");
    EXPECT_FLOAT_EQ(styleOf("d")->lineHeight, 30.0f);

    load("<div id=\"d\">x</div>", "#d { font-size: 20px; line-height: 40px; }");
    EXPECT_FLOAT_EQ(styleOf("d")->lineHeight, 40.0f);

    load("<div id=\"d\">x</div>", "#d { font-size: 20px; }");
    EXPECT_LT(styleOf("d")->lineHeight, 0.0f) << "normal stays unresolved";
    EXPECT_FLOAT_EQ(styleOf("d")->usedLineHeight(), 24.0f);
}

TEST_F(StyleEngineTest, CurrentColorResolves) {
    load("<div id=\"d\">x</div>", "#d { color: #ff0000; border-top-color: currentcolor; "
                                   "border-top-style: solid; border-top-width: 1px; }");
    EXPECT_EQ(styleOf("d")->borderColor[kTop], Color::rgba(255, 0, 0));
}

TEST_F(StyleEngineTest, BorderWidthIsZeroWithoutAStyle) {
    load("<div id=\"a\">x</div><div id=\"b\">y</div>",
         "#a { border-top-width: 5px; }"                      // no style -> not drawn
         "#b { border-top-width: 5px; border-top-style: solid; }");
    EXPECT_FLOAT_EQ(styleOf("a")->borderWidth[kTop], 0.0f);
    EXPECT_FLOAT_EQ(styleOf("b")->borderWidth[kTop], 5.0f);
}

TEST_F(StyleEngineTest, AbsolutePositioningBlockifies) {
    load("<span id=\"s\">x</span>", "#s { position: absolute; }");
    EXPECT_EQ(styleOf("s")->display, Display::Block);
    EXPECT_TRUE(styleOf("s")->isPositioned());
}

TEST_F(StyleEngineTest, StackingContextDetection) {
    load("<div id=\"plain\"></div><div id=\"faded\"></div><div id=\"moved\"></div><div id=\"layered\"></div>",
         "#faded { opacity: 0.5 } #moved { transform: translate(1px, 0) } "
         "#layered { position: relative; z-index: 3 }");
    EXPECT_FALSE(styleOf("plain")->createsStackingContext());
    EXPECT_TRUE(styleOf("faded")->createsStackingContext());
    EXPECT_TRUE(styleOf("moved")->createsStackingContext());
    EXPECT_TRUE(styleOf("layered")->createsStackingContext());
    EXPECT_EQ(styleOf("layered")->zIndex, 3);
    EXPECT_FALSE(styleOf("layered")->zIndexAuto);
}

TEST_F(StyleEngineTest, DescendantAndChildSelectors) {
    load("<div id=\"a\"><p id=\"b\"><span id=\"c\">x</span></p></div>",
         "div span { color: #010101 } div > p { color: #020202 }");
    EXPECT_EQ(styleOf("c")->color, Color::rgba(1, 1, 1));
    EXPECT_EQ(styleOf("b")->color, Color::rgba(2, 2, 2));
}

TEST_F(StyleEngineTest, AttributeSelectorsAndHiddenAttribute) {
    load("<input id=\"i\" type=\"password\" disabled><div id=\"h\" hidden>x</div>",
         "input[type=\"password\"] { color: #010101 }");
    EXPECT_EQ(styleOf("i")->color, Color::rgba(1, 1, 1));
    EXPECT_EQ(styleOf("h")->display, Display::None) << "[hidden] comes from the UA stylesheet";
}

TEST_F(StyleEngineTest, RecalculationPicksUpClassChanges) {
    load("<div id=\"d\">x</div>", ".on { color: #00ff00 } #d { color: #ff0000 }");
    EXPECT_EQ(styleOf("d")->color, Color::rgba(255, 0, 0));

    element("d")->addClass("on");
    recalc();
    EXPECT_EQ(styleOf("d")->color, Color::rgba(255, 0, 0)) << "#d still wins on specificity";

    element("d")->setAttribute(Atom("style"), "color: #0000ff");
    recalc();
    EXPECT_EQ(styleOf("d")->color, Color::rgba(0, 0, 255));
}

TEST_F(StyleEngineTest, InheritedChangePropagatesToDescendants) {
    load("<div id=\"outer\"><div id=\"inner\"><span id=\"deep\">x</span></div></div>", "");
    EXPECT_EQ(styleOf("deep")->color, Color::black());

    element("outer")->setAttribute(Atom("style"), "color: #00ff00");
    recalc();
    EXPECT_EQ(styleOf("deep")->color, Color::rgba(0, 255, 0)) << "inherited colour reaches the whole subtree";
}

TEST_F(StyleEngineTest, NewElementsGetStyled) {
    load("<div id=\"root\"></div>", ".item { color: #123456 }");
    RefPtr<dom::Element> item = document_->createElement("span");
    item->setAttribute(Atom("id"), "new");
    item->setClassName("item");
    element("root")->appendChild(*item);
    recalc();
    EXPECT_EQ(styleOf("new")->color, Color::rgba(0x12, 0x34, 0x56));
}

TEST_F(StyleEngineTest, StyleElementsAreCollected) {
    document_ = makeRef<dom::Document>();
    html::LexborHtmlParser parser;
    ASSERT_TRUE(parser.parseDocument("<style>#d { color: #abcdef }</style><div id=\"d\">x</div>", *document_));
    engine_ = std::make_unique<StyleEngine>(*document_);
    engine_->reloadStyleSheets();
    engine_->recalcStyles(800.0f, 600.0f);
    EXPECT_EQ(styleOf("d")->color, Color::rgba(0xAB, 0xCD, 0xEF));
}

TEST_F(StyleEngineTest, TransformIsComposed) {
    load("<div id=\"d\"></div>", "#d { transform: translate(10px, 20px) scale(2) }");
    const Transform& transform = styleOf("d")->transform;
    EXPECT_FALSE(transform.identity);
    EXPECT_FLOAT_EQ(transform.a, 2.0f);
    EXPECT_FLOAT_EQ(transform.d, 2.0f);
    EXPECT_FLOAT_EQ(transform.e, 10.0f);
    EXPECT_FLOAT_EQ(transform.f, 20.0f);
}

TEST_F(StyleEngineTest, BoxShadowParsesIntoTypedValues) {
    load("<div id=\"d\"></div>", "#d { box-shadow: 2px 3px 4px 1px rgba(0,0,0,0.5) }");
    ASSERT_EQ(styleOf("d")->boxShadow.size(), 1u);
    const BoxShadow& shadow = styleOf("d")->boxShadow.front();
    EXPECT_FLOAT_EQ(shadow.offsetX, 2.0f);
    EXPECT_FLOAT_EQ(shadow.offsetY, 3.0f);
    EXPECT_FLOAT_EQ(shadow.blur, 4.0f);
    EXPECT_FLOAT_EQ(shadow.spread, 1.0f);
    EXPECT_EQ(shadow.color.a, 128);
}

TEST_F(StyleEngineTest, UnsupportedPropertiesAreReported) {
    load("<div id=\"d\"></div>", "#d { grid-template-columns: 1fr 1fr; color: #010101 }");
    EXPECT_EQ(styleOf("d")->color, Color::rgba(1, 1, 1)) << "the valid declaration still applies";
    bool reported = false;
    for (const std::string& warning : engine_->warnings()) {
        if (warning.find("grid-template-columns") != std::string::npos) {
            reported = true;
        }
    }
    EXPECT_TRUE(reported);
}
