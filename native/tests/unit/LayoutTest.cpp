#include "css/StyleEngine.h"
#include "dom/Document.h"
#include "dom/Element.h"
#include "html/LexborHtmlParser.h"
#include "layout/LayoutEngine.h"

#include <gtest/gtest.h>

#include <memory>

using namespace xgu;
using namespace xgu::layout;

namespace {

// Box geometry only: these tests must not depend on the machine's fonts, so
// every fixture sizes its boxes explicitly.
class LayoutTest : public ::testing::Test {
protected:
    void load(std::string_view html, std::string_view css, float width = 800.0f, float height = 600.0f) {
        document_ = makeRef<dom::Document>();
        html::LexborHtmlParser parser;
        ASSERT_TRUE(parser.parseDocument(html, *document_));
        styleEngine_ = std::make_unique<css::StyleEngine>(*document_);
        // A predictable baseline: no default body margin in these tests.
        styleEngine_->addStyleSheet("html, body { margin: 0; padding: 0; }");
        if (!css.empty()) {
            styleEngine_->addStyleSheet(css);
        }
        styleEngine_->recalcStyles(width, height);
        layoutEngine_ = std::make_unique<LayoutEngine>(*document_);
        layoutEngine_->layout(width, height);
    }

    void relayout(float width = 800.0f, float height = 600.0f) {
        styleEngine_->recalcStyles(width, height);
        layoutEngine_->layout(width, height);
    }

    LayoutBox* box(std::string_view id) {
        dom::Element* element = document_->getElementById(id);
        return element ? layoutEngine_->boxFor(*element) : nullptr;
    }

    Rect frame(std::string_view id) {
        LayoutBox* found = box(id);
        return found ? found->borderBox() : Rect{};
    }

    RefPtr<dom::Document> document_;
    std::unique_ptr<css::StyleEngine> styleEngine_;
    std::unique_ptr<LayoutEngine> layoutEngine_;
};

} // namespace

TEST_F(LayoutTest, BlocksStackVerticallyAndFillTheWidth) {
    load("<div id=\"a\"></div><div id=\"b\"></div>", "#a { height: 50px } #b { height: 30px }");
    EXPECT_FLOAT_EQ(frame("a").x, 0.0f);
    EXPECT_FLOAT_EQ(frame("a").y, 0.0f);
    EXPECT_FLOAT_EQ(frame("a").width, 800.0f);
    EXPECT_FLOAT_EQ(frame("a").height, 50.0f);
    EXPECT_FLOAT_EQ(frame("b").y, 50.0f);
    EXPECT_FLOAT_EQ(frame("b").height, 30.0f);
}

TEST_F(LayoutTest, WidthHeightAndPercentages) {
    load("<div id=\"outer\"><div id=\"inner\"></div></div>",
         "#outer { width: 400px; height: 200px } #inner { width: 50%; height: 25% }");
    EXPECT_FLOAT_EQ(frame("outer").width, 400.0f);
    EXPECT_FLOAT_EQ(frame("inner").width, 200.0f);
    EXPECT_FLOAT_EQ(frame("inner").height, 50.0f);
}

TEST_F(LayoutTest, PaddingBorderAndContentBox) {
    load("<div id=\"d\"></div>",
         "#d { width: 100px; height: 40px; padding: 10px; border: 2px solid black }");
    // content-box: width is the content width, padding and border add to it.
    EXPECT_FLOAT_EQ(frame("d").width, 100.0f + 20.0f + 4.0f);
    EXPECT_FLOAT_EQ(frame("d").height, 40.0f + 20.0f + 4.0f);

    LayoutBox* target = box("d");
    ASSERT_NE(target, nullptr);
    EXPECT_FLOAT_EQ(target->borderEdge(css::kTop), 2.0f);
    EXPECT_FLOAT_EQ(target->paddingEdge(css::kLeft), 10.0f);
    const Rect content = target->contentBox();
    EXPECT_FLOAT_EQ(content.x, 12.0f);
    EXPECT_FLOAT_EQ(content.width, 100.0f);
}

TEST_F(LayoutTest, BorderBoxSizingIncludesPaddingAndBorder) {
    load("<div id=\"d\"></div>",
         "#d { box-sizing: border-box; width: 100px; height: 40px; padding: 10px; border: 2px solid black }");
    EXPECT_FLOAT_EQ(frame("d").width, 100.0f);
    EXPECT_FLOAT_EQ(frame("d").height, 40.0f);
    EXPECT_FLOAT_EQ(box("d")->contentBox().width, 100.0f - 20.0f - 4.0f);
}

TEST_F(LayoutTest, MarginsOffsetAndStack) {
    load("<div id=\"a\"></div><div id=\"b\"></div>",
         "#a { height: 10px; margin: 5px } #b { height: 10px; margin-left: 20px }");
    EXPECT_FLOAT_EQ(frame("a").x, 5.0f);
    EXPECT_FLOAT_EQ(frame("a").y, 5.0f);
    EXPECT_FLOAT_EQ(frame("a").width, 800.0f - 10.0f);
    // No margin collapsing (documented deviation): 5 + 10 + 5 = 20.
    EXPECT_FLOAT_EQ(frame("b").y, 20.0f);
    EXPECT_FLOAT_EQ(frame("b").x, 20.0f);
}

TEST_F(LayoutTest, FlexRowDistributesChildren) {
    load("<div id=\"row\"><div id=\"a\"></div><div id=\"b\"></div></div>",
         "#row { display: flex; width: 300px; height: 50px }"
         "#a { width: 100px } #b { flex: 1 }");
    EXPECT_FLOAT_EQ(frame("a").x, 0.0f);
    EXPECT_FLOAT_EQ(frame("a").width, 100.0f);
    EXPECT_FLOAT_EQ(frame("b").x, 100.0f);
    EXPECT_FLOAT_EQ(frame("b").width, 200.0f) << "flex: 1 takes the remaining space";
    EXPECT_FLOAT_EQ(frame("a").height, 50.0f) << "align-items: stretch by default";
}

TEST_F(LayoutTest, FlexJustifyAndAlign) {
    load("<div id=\"row\"><div id=\"a\"></div></div>",
         "#row { display: flex; width: 300px; height: 100px; justify-content: center; align-items: center }"
         "#a { width: 100px; height: 20px }");
    EXPECT_FLOAT_EQ(frame("a").x, 100.0f);
    EXPECT_FLOAT_EQ(frame("a").y, 40.0f);
}

TEST_F(LayoutTest, FlexColumnAndGap) {
    load("<div id=\"col\"><div id=\"a\"></div><div id=\"b\"></div></div>",
         "#col { display: flex; flex-direction: column; width: 100px; gap: 8px }"
         "#a { height: 20px } #b { height: 30px }");
    EXPECT_FLOAT_EQ(frame("a").y, 0.0f);
    EXPECT_FLOAT_EQ(frame("b").y, 28.0f);
}

TEST_F(LayoutTest, FlexGrowAndShrinkShareSpace) {
    load("<div id=\"row\"><div id=\"a\"></div><div id=\"b\"></div></div>",
         "#row { display: flex; width: 300px; height: 10px }"
         "#a { flex-grow: 1 } #b { flex-grow: 2 }");
    EXPECT_FLOAT_EQ(frame("a").width, 100.0f);
    EXPECT_FLOAT_EQ(frame("b").width, 200.0f);
}

TEST_F(LayoutTest, MinAndMaxConstraints) {
    load("<div id=\"row\"><div id=\"a\"></div></div>",
         "#row { display: flex; width: 300px; height: 10px }"
         "#a { flex-grow: 1; max-width: 120px }");
    EXPECT_FLOAT_EQ(frame("a").width, 120.0f);

    load("<div id=\"d\"></div>", "#d { width: 10px; min-width: 60px; height: 10px }");
    EXPECT_FLOAT_EQ(frame("d").width, 60.0f);
}

TEST_F(LayoutTest, AbsolutePositioningUsesInsets) {
    load("<div id=\"parent\"><div id=\"child\"></div></div>",
         "#parent { position: relative; width: 200px; height: 100px }"
         "#child { position: absolute; left: 30px; top: 10px; width: 50px; height: 20px }");
    EXPECT_FLOAT_EQ(frame("child").x, 30.0f);
    EXPECT_FLOAT_EQ(frame("child").y, 10.0f);
    EXPECT_FLOAT_EQ(frame("child").width, 50.0f);
}

TEST_F(LayoutTest, AbsoluteRightAndBottom) {
    load("<div id=\"parent\"><div id=\"child\"></div></div>",
         "#parent { position: relative; width: 200px; height: 100px }"
         "#child { position: absolute; right: 10px; bottom: 5px; width: 40px; height: 20px }");
    EXPECT_FLOAT_EQ(frame("child").x, 150.0f);
    EXPECT_FLOAT_EQ(frame("child").y, 75.0f);
}

TEST_F(LayoutTest, RelativePositioningShiftsTheBox) {
    load("<div id=\"a\"></div><div id=\"b\"></div>",
         "#a { height: 20px } #b { height: 20px; position: relative; left: 15px; top: 5px }");
    EXPECT_FLOAT_EQ(frame("b").x, 15.0f);
    EXPECT_FLOAT_EQ(frame("b").y, 25.0f);
}

TEST_F(LayoutTest, DisplayNoneGeneratesNoBox) {
    load("<div id=\"a\"></div><div id=\"gone\"></div><div id=\"b\"></div>",
         "#a { height: 10px } #gone { display: none; height: 100px } #b { height: 10px }");
    EXPECT_EQ(box("gone"), nullptr);
    EXPECT_FLOAT_EQ(frame("b").y, 10.0f) << "the hidden box takes no space";
}

TEST_F(LayoutTest, DisplayContentsHoistsChildren) {
    load("<div id=\"wrap\"><div id=\"ghost\"><div id=\"a\"></div></div></div>",
         "#wrap { width: 100px } #ghost { display: contents } #a { height: 25px }");
    EXPECT_EQ(box("ghost"), nullptr);
    EXPECT_FLOAT_EQ(frame("a").height, 25.0f);
    EXPECT_FLOAT_EQ(frame("a").width, 100.0f) << "the child stretches inside the grandparent";
}

TEST_F(LayoutTest, ViewportUnitsAndRelayout) {
    load("<div id=\"d\"></div>", "#d { width: 50vw; height: 10vh }", 1000.0f, 400.0f);
    EXPECT_FLOAT_EQ(frame("d").width, 500.0f);
    EXPECT_FLOAT_EQ(frame("d").height, 40.0f);

    relayout(600.0f, 200.0f);
    EXPECT_FLOAT_EQ(frame("d").width, 300.0f);
    EXPECT_FLOAT_EQ(frame("d").height, 20.0f);
}

TEST_F(LayoutTest, EmUnitsUseTheElementFontSize) {
    load("<div id=\"d\"></div>", "#d { font-size: 20px; width: 3em; height: 1em }");
    EXPECT_FLOAT_EQ(frame("d").width, 60.0f);
    EXPECT_FLOAT_EQ(frame("d").height, 20.0f);
}

TEST_F(LayoutTest, NestedFlexLayoutsCompose) {
    load("<div id=\"root\"><div id=\"side\"></div><div id=\"main\"><div id=\"inner\"></div></div></div>",
         "#root { display: flex; width: 400px; height: 200px }"
         "#side { width: 100px }"
         "#main { flex: 1; display: flex; flex-direction: column; padding: 10px }"
         "#inner { flex: 1 }");
    EXPECT_FLOAT_EQ(frame("main").x, 100.0f);
    EXPECT_FLOAT_EQ(frame("main").width, 300.0f);
    EXPECT_FLOAT_EQ(frame("inner").x, 110.0f);
    EXPECT_FLOAT_EQ(frame("inner").y, 10.0f);
    EXPECT_FLOAT_EQ(frame("inner").height, 180.0f);
}

TEST_F(LayoutTest, StyleChangeIsReflectedOnRelayout) {
    load("<div id=\"d\"></div>", "#d { width: 100px; height: 10px }");
    EXPECT_FLOAT_EQ(frame("d").width, 100.0f);

    document_->getElementById("d")->setAttribute(Atom("style"), "width: 250px");
    relayout();
    EXPECT_FLOAT_EQ(frame("d").width, 250.0f);
}

TEST_F(LayoutTest, AddingAnElementExtendsTheLayout) {
    load("<div id=\"root\"><div id=\"a\"></div></div>", "#a { height: 10px } .item { height: 30px }");
    EXPECT_FLOAT_EQ(frame("root").height, 10.0f);

    RefPtr<dom::Element> item = document_->createElement("div");
    item->setAttribute(Atom("id"), "b");
    item->setClassName("item");
    document_->getElementById("root")->appendChild(*item);
    layoutEngine_->invalidateTree();
    relayout();

    EXPECT_FLOAT_EQ(frame("b").y, 10.0f);
    EXPECT_FLOAT_EQ(frame("root").height, 40.0f);
}

TEST_F(LayoutTest, HitTestFindsTheTopmostBox) {
    load("<div id=\"outer\"><div id=\"inner\"></div></div>",
         "#outer { width: 200px; height: 100px } #inner { margin: 10px; height: 20px }");
    LayoutBox* hit = layoutEngine_->hitTest(50.0f, 20.0f);
    ASSERT_NE(hit, nullptr);
    ASSERT_NE(hit->element(), nullptr);
    EXPECT_EQ(hit->element()->id().string(), "inner");

    LayoutBox* outerHit = layoutEngine_->hitTest(50.0f, 80.0f);
    ASSERT_NE(outerHit, nullptr);
    EXPECT_EQ(outerHit->element()->id().string(), "outer");

    // <html> is laid out against the viewport, so it covers the whole surface and
    // an empty spot still hits it (documented in docs/css-support.md).
    LayoutBox* rootHit = layoutEngine_->hitTest(500.0f, 500.0f);
    ASSERT_NE(rootHit, nullptr);
    EXPECT_EQ(rootHit->element()->tagName().string(), "html");
    EXPECT_EQ(layoutEngine_->hitTest(-5.0f, 10.0f), nullptr) << "outside the viewport";
}

TEST_F(LayoutTest, PointerEventsNoneIsSkippedByHitTesting) {
    load("<div id=\"outer\"><div id=\"ghost\"></div></div>",
         "#outer { width: 200px; height: 100px } #ghost { height: 50px; pointer-events: none }");
    LayoutBox* hit = layoutEngine_->hitTest(50.0f, 20.0f);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit->element()->id().string(), "outer");
}

TEST_F(LayoutTest, ImageUsesItsAttributeSize) {
    load("<img id=\"i\" width=\"64\" height=\"32\">", "");
    EXPECT_FLOAT_EQ(frame("i").width, 64.0f);
    EXPECT_FLOAT_EQ(frame("i").height, 32.0f);
}
