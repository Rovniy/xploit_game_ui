#include "layout/LayoutBox.h"

#include <algorithm>

#include <yoga/YGNode.h>

namespace xgu::layout {

LayoutBox::LayoutBox(BoxKind kind, dom::Element* element, const css::ComputedStyle* style)
    : kind_(kind), element_(element), style_(style) {
    yogaNode_ = YGNodeNew();
    YGNodeSetContext(yogaNode_, this);
}

LayoutBox::~LayoutBox() {
    children_.clear();
    atomicInlines_.clear();
    if (yogaNode_) {
        YGNodeSetContext(yogaNode_, nullptr);
        YGNodeSetMeasureFunc(yogaNode_, nullptr);
        // Detach before freeing so Yoga does not touch a freed parent.
        if (YGNodeRef owner = YGNodeGetOwner(yogaNode_)) {
            YGNodeRemoveChild(owner, yogaNode_);
        }
        YGNodeFree(yogaNode_);
        yogaNode_ = nullptr;
    }
}

LayoutBox& LayoutBox::addChild(std::unique_ptr<LayoutBox> child) {
    child->parent_ = this;
    LayoutBox& reference = *child;
    YGNodeInsertChild(yogaNode_, child->yogaNode_, static_cast<size_t>(YGNodeGetChildCount(yogaNode_)));
    children_.push_back(std::move(child));
    return reference;
}

void LayoutBox::clearChildren() {
    while (YGNodeGetChildCount(yogaNode_) > 0) {
        YGNodeRemoveChild(yogaNode_, YGNodeGetChild(yogaNode_, 0));
    }
    children_.clear();
}

Rect LayoutBox::paddingBox() const {
    return borderBox_.inset(borderEdges_[css::kTop], borderEdges_[css::kRight], borderEdges_[css::kBottom],
                            borderEdges_[css::kLeft]);
}

Rect LayoutBox::contentBox() const {
    return borderBox_.inset(borderEdges_[css::kTop] + paddingEdges_[css::kTop],
                            borderEdges_[css::kRight] + paddingEdges_[css::kRight],
                            borderEdges_[css::kBottom] + paddingEdges_[css::kBottom],
                            borderEdges_[css::kLeft] + paddingEdges_[css::kLeft]);
}

LayoutBox& LayoutBox::addAtomicInline(std::unique_ptr<LayoutBox> box) {
    box->parent_ = this;
    LayoutBox& reference = *box;
    atomicInlines_.push_back(std::move(box));
    return reference;
}

void LayoutBox::setScrollSize(float width, float height) {
    const Rect padding = paddingBox();
    scrollWidth_ = std::max(width, padding.width);
    scrollHeight_ = std::max(height, padding.height);
    // The content may have shrunk under the current offset.
    setScroll(scrollLeft_, scrollTop_);
}

float LayoutBox::maxScrollLeft() const { return std::max(0.0f, scrollWidth_ - paddingBox().width); }

float LayoutBox::maxScrollTop() const { return std::max(0.0f, scrollHeight_ - paddingBox().height); }

bool LayoutBox::setScroll(float left, float top) {
    const float clampedLeft = std::clamp(left, 0.0f, maxScrollLeft());
    const float clampedTop = std::clamp(top, 0.0f, maxScrollTop());
    if (clampedLeft == scrollLeft_ && clampedTop == scrollTop_) {
        return false;
    }
    scrollLeft_ = clampedLeft;
    scrollTop_ = clampedTop;
    return true;
}

bool LayoutBox::scrollsHorizontally() const {
    return style_ && style_->overflowX != css::Overflow::Visible && style_->overflowX != css::Overflow::Hidden &&
           maxScrollLeft() > 0.0f;
}

bool LayoutBox::scrollsVertically() const {
    return style_ && style_->overflowY != css::Overflow::Visible && style_->overflowY != css::Overflow::Hidden &&
           maxScrollTop() > 0.0f;
}

text::InlineContent& LayoutBox::ensureInlineContent() {
    if (!inlineContent_) {
        inlineContent_ = std::make_unique<text::InlineContent>();
    }
    return *inlineContent_;
}

} // namespace xgu::layout
