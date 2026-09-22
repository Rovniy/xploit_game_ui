// Stage 6: the editing model behind <input> and <textarea>.
//
// Offsets here are UTF-16 code units, because that is what skparagraph counts
// in; the DOM boundary converts to UTF-8.

#include "dom/Document.h"
#include "dom/Element.h"
#include "dom/TextControl.h"

#include <gtest/gtest.h>

using xgu::Atom;
using xgu::dom::Document;
using xgu::dom::Element;
using xgu::dom::TextControl;

namespace {

class TextControlTest : public ::testing::Test {
protected:
    void SetUp() override { document_ = xgu::makeRef<Document>(); }

    // Creates an element and returns its control. `attributes` is a list of
    // name/value pairs applied before the control is made.
    TextControl& control(const char* tag, std::initializer_list<std::pair<const char*, const char*>> attributes = {}) {
        element_ = xgu::makeRef<Element>(document_.get(), Atom(tag));
        for (const auto& [name, value] : attributes) {
            element_->setAttribute(Atom(name), value);
        }
        return element_->ensureTextControl();
    }

    xgu::RefPtr<Document> document_;
    xgu::RefPtr<Element> element_;
};

} // namespace

TEST_F(TextControlTest, Utf8AndUtf16RoundTrip) {
    const std::string ascii = "hello";
    EXPECT_EQ(xgu::dom::utf16ToUtf8(xgu::dom::utf8ToUtf16(ascii)), ascii);

    const std::string cyrillic = "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"; // "привет"
    const std::u16string wide = xgu::dom::utf8ToUtf16(cyrillic);
    EXPECT_EQ(wide.size(), 6u) << "one code unit per Cyrillic letter";
    EXPECT_EQ(xgu::dom::utf16ToUtf8(wide), cyrillic);

    const std::string emoji = "\xF0\x9F\x8E\xAE"; // U+1F3AE, outside the BMP
    const std::u16string pair = xgu::dom::utf8ToUtf16(emoji);
    EXPECT_EQ(pair.size(), 2u) << "a surrogate pair";
    EXPECT_EQ(xgu::dom::utf16ToUtf8(pair), emoji);
}

TEST_F(TextControlTest, StartsFromTheValueAttribute) {
    TextControl& field = control("input", {{"value", "start"}});
    EXPECT_EQ(field.value(), "start");
    EXPECT_EQ(field.caret(), 5u) << "the caret sits at the end";
    EXPECT_FALSE(field.hasSelection());
}

TEST_F(TextControlTest, InsertsAtTheCaret) {
    TextControl& field = control("input");
    EXPECT_TRUE(field.insertText(u"abc"));
    EXPECT_EQ(field.value(), "abc");
    field.collapseTo(1);
    EXPECT_TRUE(field.insertText(u"X"));
    EXPECT_EQ(field.value(), "aXbc");
    EXPECT_EQ(field.caret(), 2u);
}

TEST_F(TextControlTest, InsertingReplacesTheSelection) {
    TextControl& field = control("input", {{"value", "hello"}});
    field.setSelection(1, 4);
    EXPECT_TRUE(field.insertText(u"EY"));
    EXPECT_EQ(field.value(), "hEYo");
    EXPECT_FALSE(field.hasSelection());
    EXPECT_EQ(field.caret(), 3u);
}

TEST_F(TextControlTest, DeletesBackwardAndForward) {
    TextControl& field = control("input", {{"value", "abcd"}});
    field.collapseTo(2);
    EXPECT_TRUE(field.deleteBackward());
    EXPECT_EQ(field.value(), "acd");
    EXPECT_EQ(field.caret(), 1u);
    EXPECT_TRUE(field.deleteForward());
    EXPECT_EQ(field.value(), "ad");

    field.collapseTo(0);
    EXPECT_FALSE(field.deleteBackward()) << "nothing before the start";
    field.moveToEnd(false);
    EXPECT_FALSE(field.deleteForward()) << "nothing after the end";
}

TEST_F(TextControlTest, DeleteRemovesTheWholeSelection) {
    TextControl& field = control("input", {{"value", "abcdef"}});
    field.setSelection(1, 4);
    EXPECT_TRUE(field.deleteBackward());
    EXPECT_EQ(field.value(), "aef");
    EXPECT_EQ(field.caret(), 1u);
}

TEST_F(TextControlTest, SurrogatePairsMoveAndDeleteAsOneCharacter) {
    TextControl& field = control("input");
    field.insertText(xgu::dom::utf8ToUtf16("a\xF0\x9F\x8E\xAE" "b"));
    ASSERT_EQ(field.text().size(), 4u) << "a + surrogate pair + b";

    field.moveToEnd(false);
    field.moveLeft(false);
    EXPECT_EQ(field.caret(), 3u) << "past the 'b'";
    field.moveLeft(false);
    EXPECT_EQ(field.caret(), 1u) << "the pair is skipped whole";

    field.moveToEnd(false);
    field.deleteBackward(); // 'b'
    field.deleteBackward(); // the emoji
    EXPECT_EQ(field.value(), "a");
}

TEST_F(TextControlTest, ShiftMovementExtendsTheSelection) {
    TextControl& field = control("input", {{"value", "abcdef"}});
    field.collapseTo(2);
    field.moveRight(true);
    field.moveRight(true);
    EXPECT_TRUE(field.hasSelection());
    EXPECT_EQ(field.selectionStart(), 2u);
    EXPECT_EQ(field.selectionEnd(), 4u);

    field.moveLeft(false);
    EXPECT_FALSE(field.hasSelection()) << "moving without shift collapses";
    EXPECT_EQ(field.caret(), 2u) << "to the start of what was selected";
}

TEST_F(TextControlTest, HomeAndEndAndSelectAll) {
    TextControl& field = control("input", {{"value", "abcdef"}});
    field.moveToStart(false);
    EXPECT_EQ(field.caret(), 0u);
    field.moveToEnd(true);
    EXPECT_EQ(field.selectionStart(), 0u);
    EXPECT_EQ(field.selectionEnd(), 6u);

    field.collapseTo(3);
    field.selectAll();
    EXPECT_EQ(field.selectionStart(), 0u);
    EXPECT_EQ(field.selectionEnd(), 6u);
}

TEST_F(TextControlTest, SingleLineFieldsFlattenNewlines) {
    TextControl& field = control("input");
    field.insertText(u"one\ntwo");
    EXPECT_EQ(field.value(), "one two");

    TextControl& area = control("textarea");
    area.insertText(u"one\ntwo");
    EXPECT_EQ(area.value(), "one\ntwo");
    EXPECT_TRUE(area.isMultiline());
}

TEST_F(TextControlTest, PasswordsAreMaskedButKeepTheirValue) {
    TextControl& field = control("input", {{"type", "password"}, {"value", "secret"}});
    EXPECT_TRUE(field.isPassword());
    EXPECT_EQ(field.value(), "secret") << "the value itself is not masked";
    EXPECT_EQ(field.displayText().size(), 6u);
    EXPECT_EQ(field.displayText()[0], u'•');
}

TEST_F(TextControlTest, PlaceholderShowsOnlyWhenEmpty) {
    TextControl& field = control("input", {{"placeholder", "type here"}});
    EXPECT_TRUE(field.showingPlaceholder());
    EXPECT_EQ(xgu::dom::utf16ToUtf8(field.displayText()), "type here");

    field.insertText(u"x");
    EXPECT_FALSE(field.showingPlaceholder());
    EXPECT_EQ(xgu::dom::utf16ToUtf8(field.displayText()), "x");
}

TEST_F(TextControlTest, NonTextInputTypesAreNotEditable) {
    EXPECT_TRUE(control("input").isEditable()) << "text is the default type";
    EXPECT_TRUE(control("input", {{"type", "password"}}).isEditable());
    EXPECT_TRUE(control("textarea").isEditable());
    EXPECT_FALSE(control("input", {{"type", "checkbox"}}).isEditable());
    EXPECT_FALSE(control("input", {{"type", "button"}}).isEditable());
}

TEST_F(TextControlTest, TracksWhetherTheValueChangedSinceItWasCommitted) {
    TextControl& field = control("input", {{"value", "a"}});
    EXPECT_FALSE(field.changedSinceCommit());
    field.insertText(u"b");
    EXPECT_TRUE(field.changedSinceCommit());
    field.markCommitted();
    EXPECT_FALSE(field.changedSinceCommit());
}

TEST_F(TextControlTest, OffsetsConvertBetweenUtf8AndUtf16) {
    TextControl& field = control("input");
    // "пр" is two code units and four bytes.
    field.setValue("\xD0\xBF\xD1\x80z");
    EXPECT_EQ(field.utf16ToUtf8Offset(2), 4u);
    EXPECT_EQ(field.utf8ToUtf16Offset(4), 2u);
    EXPECT_EQ(field.utf16ToUtf8Offset(3), 5u);
}
