#pragma once

#include "css/ComputedStyle.h"
#include "layout/Rect.h"

#include <modules/skparagraph/include/Paragraph.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class SkCanvas;

namespace xgu::dom {
class Element;
class Node;
} // namespace xgu::dom

namespace xgu::layout {
class LayoutBox;
}

namespace xgu::text {

// One atomic inline (an <img>, a button, an inline-block) placed inside the text
// as a placeholder of a fixed size.
struct InlinePlaceholder {
    layout::LayoutBox* box = nullptr;
    float width = 0.0f;
    float height = 0.0f;
    // Byte offset in the container's text where this box belongs, so the
    // paragraph puts it in document order rather than at the end.
    size_t textOffset = 0;
    // Filled after layout: position relative to the paragraph origin.
    float x = 0.0f;
    float y = 0.0f;
};

// The text of one inline formatting context: the runs collected from a block's
// inline children, shaped and measured through skparagraph.
class InlineContent {
public:
    InlineContent();
    ~InlineContent();

    // Rebuilds the runs from `container`'s inline descendants. `containerStyle`
    // supplies the inherited text properties.
    void build(const dom::Element& container, const css::ComputedStyle& containerStyle,
               std::vector<InlinePlaceholder> placeholders);

    // Builds from one literal string, with no white-space collapsing: the value
    // of a text control is shown exactly as it is.
    void buildLiteral(const css::ComputedStyle& containerStyle, std::string utf8);

    bool empty() const { return text_.empty() && placeholders_.empty(); }
    const std::string& text() const { return text_; }

    // Lays the paragraph out at `width` (in CSS px) and returns its size.
    // Results are cached for the last two widths.
    struct Size {
        float width = 0.0f;
        float height = 0.0f;
    };
    Size layout(float width);

    // Width the content needs without any wrapping, and the widest unbreakable
    // piece; Yoga uses these for min/max content sizing.
    float maxIntrinsicWidth();
    float minIntrinsicWidth();
    // Baseline of the first line, measured from the top.
    float firstBaseline();

    // Paints the last laid-out paragraph (Stage 5 uses this).
    void paint(SkCanvas* canvas, float x, float y);

    // --- caret and selection geometry, in UTF-16 code units -----------------
    // Boxes covering [start, end), relative to the paragraph origin.
    std::vector<layout::Rect> rectsForRange(size_t start, size_t end);
    // A one-pixel-wide caret box at `offset`.
    layout::Rect caretRect(size_t offset);
    // Offset nearest to a point given relative to the paragraph origin.
    size_t offsetAtPoint(float x, float y);

    const std::vector<InlinePlaceholder>& placeholders() const { return placeholders_; }
    // Updates the reserved sizes (the boxes are measured by the layout engine).
    void setPlaceholderSizes(const std::vector<InlinePlaceholder>& sizes);

    // Invalidates the shaped paragraph (text or style changed).
    void invalidate();

    // Length of the text in UTF-16 code units, which is what the paragraph and
    // the text controls count in.
    size_t utf16Length() const;

private:
    void ensureParagraph();

    std::string text_; // UTF-8, after white-space processing
    struct Run {
        size_t start = 0; // byte offset into text_
        size_t length = 0;
        css::StyleValues style;
    };
    std::vector<Run> runs_;
    std::vector<InlinePlaceholder> placeholders_;
    css::StyleValues containerStyle_;
    std::unique_ptr<skia::textlayout::Paragraph> paragraph_;
    float laidOutWidth_ = -1.0f;
    Size lastSize_;
    bool dirty_ = true;
};

// White-space processing per CSS `white-space`. Returns the text that should be
// shaped; `collapsed` reports whether spaces were collapsed.
std::string processWhiteSpace(std::string_view text, css::WhiteSpace mode, bool trimLeading, bool trimTrailing);

// Applies `text-transform`.
std::string applyTextTransform(std::string_view text, css::TextTransform transform);

} // namespace xgu::text
