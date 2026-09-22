#include "layout/LayoutBox.h"

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

text::InlineContent& LayoutBox::ensureInlineContent() {
    if (!inlineContent_) {
        inlineContent_ = std::make_unique<text::InlineContent>();
    }
    return *inlineContent_;
}

} // namespace xgu::layout
