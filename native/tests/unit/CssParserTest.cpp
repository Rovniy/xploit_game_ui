#include "css/ComputedStyle.h"
#include "css/StyleSheet.h"
#include "css/Tokenizer.h"

#include <gtest/gtest.h>

using namespace xgu;
using namespace xgu::css;

namespace {

DeclarationBlock parse(std::string_view text) { return parseDeclarationBlock(text); }

const Declaration* find(const DeclarationBlock& block, PropertyId property) {
    const Declaration* last = nullptr;
    for (const Declaration& declaration : block) {
        if (declaration.property == property) {
            last = &declaration;
        }
    }
    return last;
}

float lengthPx(const DeclarationBlock& block, PropertyId property) {
    const Declaration* declaration = find(block, property);
    return declaration && declaration->value.isLength() ? declaration->value.length.value : -12345.0f;
}

} // namespace

TEST(CssTokenizer, ProducesTheExpectedTokens) {
    Tokenizer tokenizer("div#id.cls { width: 10px; color: #fff }");
    EXPECT_EQ(tokenizer.nextSkippingSpace().type, TokenType::Ident);
    EXPECT_EQ(tokenizer.nextSkippingSpace().type, TokenType::Hash);
    Token delim = tokenizer.nextSkippingSpace();
    EXPECT_TRUE(delim.isDelim('.'));
    EXPECT_EQ(tokenizer.nextSkippingSpace().type, TokenType::Ident);
    EXPECT_EQ(tokenizer.nextSkippingSpace().type, TokenType::LeftBrace);
}

TEST(CssTokenizer, NumbersUnitsStringsAndComments) {
    Tokenizer tokenizer("/* skip */ 12px 50% 1.5 \"text\" url(a/b.png) rgba(");
    Token token = tokenizer.nextSkippingSpace();
    EXPECT_EQ(token.type, TokenType::Dimension);
    EXPECT_EQ(token.unit, "px");
    EXPECT_DOUBLE_EQ(token.number, 12.0);

    token = tokenizer.nextSkippingSpace();
    EXPECT_EQ(token.type, TokenType::Percentage);
    EXPECT_DOUBLE_EQ(token.number, 50.0);

    token = tokenizer.nextSkippingSpace();
    EXPECT_EQ(token.type, TokenType::Number);
    EXPECT_DOUBLE_EQ(token.number, 1.5);

    token = tokenizer.nextSkippingSpace();
    EXPECT_EQ(token.type, TokenType::String);
    EXPECT_EQ(token.value, "text");

    token = tokenizer.nextSkippingSpace();
    EXPECT_EQ(token.type, TokenType::Url);
    EXPECT_EQ(token.value, "a/b.png");

    token = tokenizer.nextSkippingSpace();
    EXPECT_EQ(token.type, TokenType::Function);
    EXPECT_EQ(token.value, "rgba");
}

TEST(CssParser, LengthsAndUnits) {
    const DeclarationBlock block = parse("width: 10px; height: 50%; margin-top: 2em; padding-top: 1.5rem;"
                                          "left: -5px; min-width: 0; max-width: none; font-size: 12pt");
    EXPECT_EQ(find(block, PropertyId::Width)->value.length, Length::px(10.0f));
    EXPECT_EQ(find(block, PropertyId::Height)->value.length, Length::percent(50.0f));
    EXPECT_EQ(find(block, PropertyId::MarginTop)->value.length.unit, LengthUnit::Em);
    EXPECT_EQ(find(block, PropertyId::PaddingTop)->value.length.unit, LengthUnit::Rem);
    EXPECT_FLOAT_EQ(lengthPx(block, PropertyId::Left), -5.0f);
    EXPECT_TRUE(find(block, PropertyId::MaxWidth)->value.length.isNone());
    EXPECT_FLOAT_EQ(find(block, PropertyId::FontSize)->value.length.value, 16.0f); // 12pt = 16px
}

TEST(CssParser, RejectsInvalidValues) {
    EXPECT_TRUE(parse("width: red").empty()) << "a colour is not a length";
    EXPECT_TRUE(parse("width: 10").empty()) << "unitless lengths other than 0 are invalid";
    EXPECT_TRUE(parse("display: sideways").empty());
    EXPECT_TRUE(parse("color: notacolor").empty());
    EXPECT_TRUE(parse("padding-top: -5px").empty()) << "padding may not be negative";
    EXPECT_TRUE(parse("nonsense: 1px").empty());
    EXPECT_FALSE(parse("width: 0").empty()) << "unitless zero is allowed";
}

TEST(CssParser, Colors) {
    const DeclarationBlock block = parse("color: #abc; background-color: #11223344;"
                                          "border-top-color: rgb(1, 2, 3); border-left-color: rgba(4, 5, 6, 0.5)");
    EXPECT_EQ(find(block, PropertyId::Color)->value.color, Color::rgba(0xAA, 0xBB, 0xCC));
    EXPECT_EQ(find(block, PropertyId::BackgroundColor)->value.color, Color::rgba(0x11, 0x22, 0x33, 0x44));
    EXPECT_EQ(find(block, PropertyId::BorderTopColor)->value.color, Color::rgba(1, 2, 3));
    EXPECT_EQ(find(block, PropertyId::BorderLeftColor)->value.color.a, 128);

    const DeclarationBlock named = parse("color: red; background-color: transparent");
    EXPECT_EQ(find(named, PropertyId::Color)->value.color, Color::rgba(255, 0, 0));
    EXPECT_TRUE(find(named, PropertyId::BackgroundColor)->value.color.isTransparent());

    const DeclarationBlock hsl = parse("color: hsl(0, 100%, 50%)");
    EXPECT_EQ(find(hsl, PropertyId::Color)->value.color, Color::rgba(255, 0, 0));
}

TEST(CssParser, ExpandsBoxShorthands) {
    const DeclarationBlock one = parse("margin: 5px");
    EXPECT_FLOAT_EQ(lengthPx(one, PropertyId::MarginTop), 5.0f);
    EXPECT_FLOAT_EQ(lengthPx(one, PropertyId::MarginRight), 5.0f);
    EXPECT_FLOAT_EQ(lengthPx(one, PropertyId::MarginBottom), 5.0f);
    EXPECT_FLOAT_EQ(lengthPx(one, PropertyId::MarginLeft), 5.0f);

    const DeclarationBlock two = parse("padding: 1px 2px");
    EXPECT_FLOAT_EQ(lengthPx(two, PropertyId::PaddingTop), 1.0f);
    EXPECT_FLOAT_EQ(lengthPx(two, PropertyId::PaddingRight), 2.0f);
    EXPECT_FLOAT_EQ(lengthPx(two, PropertyId::PaddingBottom), 1.0f);
    EXPECT_FLOAT_EQ(lengthPx(two, PropertyId::PaddingLeft), 2.0f);

    const DeclarationBlock four = parse("margin: 1px 2px 3px 4px");
    EXPECT_FLOAT_EQ(lengthPx(four, PropertyId::MarginTop), 1.0f);
    EXPECT_FLOAT_EQ(lengthPx(four, PropertyId::MarginRight), 2.0f);
    EXPECT_FLOAT_EQ(lengthPx(four, PropertyId::MarginBottom), 3.0f);
    EXPECT_FLOAT_EQ(lengthPx(four, PropertyId::MarginLeft), 4.0f);

    // Later declarations win, which is why shorthands must expand at parse time.
    const DeclarationBlock ordered = parse("margin: 0; margin-top: 5px");
    EXPECT_FLOAT_EQ(lengthPx(ordered, PropertyId::MarginTop), 5.0f);
    EXPECT_FLOAT_EQ(lengthPx(ordered, PropertyId::MarginLeft), 0.0f);
}

TEST(CssParser, ExpandsBorderShorthand) {
    const DeclarationBlock block = parse("border: 2px solid #ff0000");
    EXPECT_FLOAT_EQ(lengthPx(block, PropertyId::BorderTopWidth), 2.0f);
    EXPECT_FLOAT_EQ(lengthPx(block, PropertyId::BorderLeftWidth), 2.0f);
    EXPECT_TRUE(find(block, PropertyId::BorderTopStyle)->value.isKeyword(Atom("solid")));
    EXPECT_EQ(find(block, PropertyId::BorderBottomColor)->value.color, Color::rgba(255, 0, 0));

    const DeclarationBlock side = parse("border-left: 1px dashed blue");
    EXPECT_FLOAT_EQ(lengthPx(side, PropertyId::BorderLeftWidth), 1.0f);
    EXPECT_EQ(find(side, PropertyId::BorderTopWidth), nullptr) << "only the named side is set";
}

TEST(CssParser, ExpandsFlexGapOverflowAndBackground) {
    const DeclarationBlock flex = parse("flex: 1 2 30px");
    EXPECT_FLOAT_EQ(find(flex, PropertyId::FlexGrow)->value.length.value, 1.0f);
    EXPECT_FLOAT_EQ(find(flex, PropertyId::FlexShrink)->value.length.value, 2.0f);
    EXPECT_FLOAT_EQ(lengthPx(flex, PropertyId::FlexBasis), 30.0f);

    const DeclarationBlock flexOne = parse("flex: 1");
    EXPECT_FLOAT_EQ(find(flexOne, PropertyId::FlexGrow)->value.length.value, 1.0f);
    EXPECT_FLOAT_EQ(find(flexOne, PropertyId::FlexShrink)->value.length.value, 1.0f);

    const DeclarationBlock none = parse("flex: none");
    EXPECT_FLOAT_EQ(find(none, PropertyId::FlexGrow)->value.length.value, 0.0f);
    EXPECT_FLOAT_EQ(find(none, PropertyId::FlexShrink)->value.length.value, 0.0f);

    const DeclarationBlock gap = parse("gap: 4px 8px");
    EXPECT_FLOAT_EQ(lengthPx(gap, PropertyId::RowGap), 4.0f);
    EXPECT_FLOAT_EQ(lengthPx(gap, PropertyId::ColumnGap), 8.0f);

    const DeclarationBlock overflow = parse("overflow: hidden");
    EXPECT_TRUE(find(overflow, PropertyId::OverflowX)->value.isKeyword(Atom("hidden")));
    EXPECT_TRUE(find(overflow, PropertyId::OverflowY)->value.isKeyword(Atom("hidden")));

    const DeclarationBlock background = parse("background: #101010 url(bg.png) no-repeat");
    EXPECT_EQ(find(background, PropertyId::BackgroundColor)->value.color, Color::rgba(0x10, 0x10, 0x10));
    EXPECT_EQ(find(background, PropertyId::BackgroundImage)->value.text, "bg.png");
    EXPECT_TRUE(find(background, PropertyId::BackgroundRepeat)->value.isKeyword(Atom("no-repeat")));
}

TEST(CssParser, ImportantAndCaseInsensitivity) {
    const DeclarationBlock block = parse("COLOR: RED !IMPORTANT; Width: 10PX");
    const Declaration* color = find(block, PropertyId::Color);
    ASSERT_NE(color, nullptr);
    EXPECT_TRUE(color->important);
    EXPECT_FLOAT_EQ(lengthPx(block, PropertyId::Width), 10.0f);
    EXPECT_FALSE(find(block, PropertyId::Width)->important);
}

TEST(CssParser, StyleSheetRulesAndSelectors) {
    const StyleSheet sheet = parseStyleSheet(".a, #b > span { color: red; }\n"
                                              "div { width: 1px }\n"
                                              "@media screen { p { color: blue } }\n"
                                              "bad!!selector { color: red }\n");
    // Two selectors in the first rule plus the div rule.
    EXPECT_EQ(sheet.rules.size(), 3u);
    EXPECT_EQ(sheet.rules[0].order, 0u);
    EXPECT_EQ(sheet.rules[1].order, 1u);
    EXPECT_FALSE(sheet.warnings.empty()) << "the at-rule and the bad selector are reported";
}

TEST(CssParser, TransformFunctions) {
    const DeclarationBlock block = parse("transform: translate(10px, 20px) rotate(45deg) scale(2)");
    const Declaration* declaration = find(block, PropertyId::Transform);
    ASSERT_NE(declaration, nullptr);
    ASSERT_EQ(declaration->value.type, ValueType::List);
    ASSERT_EQ(declaration->value.items.size(), 3u);
    EXPECT_EQ(declaration->value.items[0].keyword, Atom("translate"));
    EXPECT_EQ(declaration->value.items[1].keyword, Atom("rotate"));
    EXPECT_FLOAT_EQ(declaration->value.items[1].items[0].length.value, 45.0f);
}

TEST(CssParser, BoxShadowAndTextDecoration) {
    const DeclarationBlock shadow = parse("box-shadow: 2px 3px 4px 1px rgba(0,0,0,0.5)");
    const Declaration* declaration = find(shadow, PropertyId::BoxShadow);
    ASSERT_NE(declaration, nullptr);
    EXPECT_EQ(declaration->value.type, ValueType::List);

    const DeclarationBlock decoration = parse("text-decoration: underline red");
    EXPECT_NE(find(decoration, PropertyId::TextDecorationLine), nullptr);
    EXPECT_EQ(find(decoration, PropertyId::TextDecorationColor)->value.color, Color::rgba(255, 0, 0));
}

TEST(CssParser, SerializesBackToText) {
    const DeclarationBlock block = parse("color: #ff0000; width: 10px");
    const std::string text = serializeDeclarations(block);
    EXPECT_NE(text.find("color: #ff0000;"), std::string::npos) << text;
    EXPECT_NE(text.find("width: 10px;"), std::string::npos) << text;
}

TEST(CssValue, ResolvesRelativeUnits) {
    LengthContext context;
    context.fontSize = 20.0f;
    context.rootFontSize = 16.0f;
    context.viewportWidth = 800.0f;
    context.viewportHeight = 600.0f;

    EXPECT_FLOAT_EQ(resolveLength(Length::px(5.0f), context), 5.0f);
    EXPECT_FLOAT_EQ(resolveLength(Length{2.0f, LengthUnit::Em}, context), 40.0f);
    EXPECT_FLOAT_EQ(resolveLength(Length{2.0f, LengthUnit::Rem}, context), 32.0f);
    EXPECT_FLOAT_EQ(resolveLength(Length{50.0f, LengthUnit::Vw}, context), 400.0f);
    EXPECT_FLOAT_EQ(resolveLength(Length{50.0f, LengthUnit::Vh}, context), 300.0f);
    EXPECT_FLOAT_EQ(resolveLength(Length{10.0f, LengthUnit::Vmin}, context), 60.0f);
    EXPECT_FLOAT_EQ(resolveLength(Length::percent(50.0f), context, 123.0f), 123.0f) << "percent needs layout";
    EXPECT_FLOAT_EQ(resolveLength(Length::automatic(), context, 7.0f), 7.0f);
}
