#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace xgu::dom {

class Element;

// Editing state of an <input> or a <textarea>.
//
// The text is held as UTF-16 because that is what skparagraph measures and what
// its caret queries speak; the DOM and JavaScript see UTF-8. Offsets in this
// class are UTF-16 code units and always sit on code-point boundaries.
class TextControl {
public:
    explicit TextControl(Element& owner);

    bool isMultiline() const;
    bool isPassword() const;
    // An <input type="checkbox"/"radio"/"button"/...> has no editable text.
    bool isEditable() const;

    // The value, as the DOM reports it.
    std::string value() const;
    // Returns true when the value changed. The caret goes to the end, which is
    // what assigning to `value` does in a browser.
    bool setValue(std::string_view utf8);

    const std::u16string& text() const { return text_; }
    bool empty() const { return text_.empty(); }

    // What should be painted: the value, dots for a password, or the
    // placeholder when the field is empty.
    std::u16string displayText() const;
    bool showingPlaceholder() const;

    size_t caret() const { return caret_; }
    size_t anchor() const { return anchor_; }
    size_t selectionStart() const { return caret_ < anchor_ ? caret_ : anchor_; }
    size_t selectionEnd() const { return caret_ < anchor_ ? anchor_ : caret_; }
    bool hasSelection() const { return caret_ != anchor_; }

    void setSelection(size_t start, size_t end);
    void collapseTo(size_t offset);
    void selectAll();

    // --- editing; each returns true when the text changed -------------------
    bool insertText(std::u16string_view text);
    bool deleteBackward();
    bool deleteForward();
    // Replaces the selection, or inserts at the caret when there is none.
    bool deleteSelection();

    // --- caret movement; `extend` keeps the anchor (shift-selection) ---------
    void moveLeft(bool extend);
    void moveRight(bool extend);
    void moveToStart(bool extend);
    void moveToEnd(bool extend);

    // Conversions for the DOM and JavaScript boundary.
    size_t utf16ToUtf8Offset(size_t offset) const;
    size_t utf8ToUtf16Offset(size_t offset) const;

    // The value when the field was focused, so `change` can fire only on a real
    // edit, as the DOM specifies.
    void markCommitted() { committed_ = text_; }
    bool changedSinceCommit() const { return committed_ != text_; }

private:
    // Keeps offsets inside the text and off the middle of a surrogate pair.
    size_t clamp(size_t offset) const;

    Element& owner_;
    std::u16string text_;
    std::u16string committed_;
    size_t caret_ = 0;
    size_t anchor_ = 0;
};

// UTF-8 <-> UTF-16 for the text the controls and the paragraph exchange.
std::u16string utf8ToUtf16(std::string_view text);
std::string utf16ToUtf8(std::u16string_view text);

} // namespace xgu::dom
