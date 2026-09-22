#pragma once

#include "css/ComputedStyle.h"
#include "layout/Rect.h"
#include "text/InlineContent.h"

#include <yoga/Yoga.h>

#include <memory>
#include <vector>

namespace xgu::dom {
class Element;
}

namespace xgu::layout {

enum class BoxKind : uint8_t {
    Block,          // block container, laid out as a column flex line
    Flex,           // flex container
    InlineContext,  // anonymous box holding an inline formatting context
    Replaced,       // <img> and friends: intrinsic size, no children
};

// One box of the layout tree. Owned by its parent; the root is owned by
// LayoutTree. Every box has a Yoga node except the ones inside an inline
// formatting context, which the paragraph positions instead.
class LayoutBox {
public:
    LayoutBox(BoxKind kind, dom::Element* element, const css::ComputedStyle* style);
    ~LayoutBox();

    LayoutBox(const LayoutBox&) = delete;
    LayoutBox& operator=(const LayoutBox&) = delete;

    BoxKind kind() const { return kind_; }
    // Null for anonymous boxes.
    dom::Element* element() const { return element_; }
    const css::ComputedStyle* style() const { return style_; }
    void setStyle(const css::ComputedStyle* style) { style_ = style; }

    const css::ComputedStyle* styleUsedForText() const { return styleUsedForText_; }
    void setStyleUsedForText(const css::ComputedStyle* style) { styleUsedForText_ = style; }

    YGNodeRef yogaNode() const { return yogaNode_; }

    LayoutBox* parent() const { return parent_; }
    const std::vector<std::unique_ptr<LayoutBox>>& children() const { return children_; }
    LayoutBox& addChild(std::unique_ptr<LayoutBox> child);
    void clearChildren();

    // Frames in document coordinates (CSS px), filled after layout.
    const Rect& borderBox() const { return borderBox_; }
    Rect paddingBox() const;
    Rect contentBox() const;
    void setBorderBox(const Rect& rect) { borderBox_ = rect; }

    // Edge sizes resolved by Yoga.
    float borderEdge(css::Side side) const { return borderEdges_[side]; }
    float paddingEdge(css::Side side) const { return paddingEdges_[side]; }
    void setEdges(const std::array<float, 4>& border, const std::array<float, 4>& padding) {
        borderEdges_ = border;
        paddingEdges_ = padding;
    }

    text::InlineContent* inlineContent() const { return inlineContent_.get(); }
    text::InlineContent& ensureInlineContent();

    // Atomic inlines (an <img>, a button, an inline-block) inside this inline
    // formatting context. They are NOT Yoga children: a node with a measure
    // function may not have any, so the paragraph positions them instead.
    LayoutBox& addAtomicInline(std::unique_ptr<LayoutBox> box);
    const std::vector<std::unique_ptr<LayoutBox>>& atomicInlines() const { return atomicInlines_; }

    // --- scrolling -----------------------------------------------------------
    // How far the content is scrolled, in CSS pixels. Applied when painting and
    // hit testing rather than baked into the frames, so scrolling never has to
    // run layout again.
    float scrollLeft() const { return scrollLeft_; }
    float scrollTop() const { return scrollTop_; }
    // Clamps to the scrollable range; returns true when the offset moved.
    bool setScroll(float left, float top);

    // Size of the content, at least the size of the padding box.
    float scrollWidth() const { return scrollWidth_; }
    float scrollHeight() const { return scrollHeight_; }
    void setScrollSize(float width, float height);

    float maxScrollLeft() const;
    float maxScrollTop() const;
    // True when the style asks for scrolling and there is something to scroll.
    bool scrollsHorizontally() const;
    bool scrollsVertically() const;

    // Intrinsic size of a replaced box (image, and later video).
    void setIntrinsicSize(float width, float height) {
        intrinsicWidth_ = width;
        intrinsicHeight_ = height;
    }
    float intrinsicWidth() const { return intrinsicWidth_; }
    float intrinsicHeight() const { return intrinsicHeight_; }

private:
    BoxKind kind_;
    dom::Element* element_ = nullptr;
    const css::ComputedStyle* style_ = nullptr;
    // The style the inline runs were shaped from; when it no longer matches
    // style_ the element restyled and the paragraph has to be rebuilt.
    const css::ComputedStyle* styleUsedForText_ = nullptr;
    YGNodeRef yogaNode_ = nullptr;
    LayoutBox* parent_ = nullptr;
    std::vector<std::unique_ptr<LayoutBox>> children_;
    Rect borderBox_;
    std::array<float, 4> borderEdges_{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> paddingEdges_{0.0f, 0.0f, 0.0f, 0.0f};
    std::unique_ptr<text::InlineContent> inlineContent_;
    std::vector<std::unique_ptr<LayoutBox>> atomicInlines_;
    float intrinsicWidth_ = 0.0f;
    float intrinsicHeight_ = 0.0f;
    float scrollLeft_ = 0.0f;
    float scrollTop_ = 0.0f;
    float scrollWidth_ = 0.0f;
    float scrollHeight_ = 0.0f;
};

} // namespace xgu::layout
