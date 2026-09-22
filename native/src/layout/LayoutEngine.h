#pragma once

#include "layout/LayoutBox.h"

#include <memory>

namespace xgu::dom {
class Document;
class Element;
} // namespace xgu::dom

namespace xgu::layout {

// Builds the box tree from the styled DOM and runs Yoga over it.
//
// Block containers are laid out as column flex containers (documented deviation
// in docs/css-support.md); inline content of a block becomes one anonymous
// InlineContext box measured through skparagraph.
class LayoutEngine {
public:
    explicit LayoutEngine(dom::Document& document);
    ~LayoutEngine();

    // Rebuilds the box tree where the DOM marked it dirty, then lays everything
    // out for the given viewport (CSS px). Safe to call every frame.
    void layout(float viewportWidth, float viewportHeight, float devicePixelRatio = 1.0f);

    LayoutBox* root() const { return root_.get(); }
    // Box of an element, or nullptr when it generates none (display:none).
    LayoutBox* boxFor(const dom::Element& element) const;

    // Topmost box containing the point, in document coordinates.
    LayoutBox* hitTest(float x, float y) const;

    float viewportWidth() const { return viewportWidth_; }
    float viewportHeight() const { return viewportHeight_; }

    // Forces a full rebuild on the next layout() call.
    void invalidateTree() { treeDirty_ = true; }

private:
    void rebuildTree();
    std::unique_ptr<LayoutBox> buildBox(dom::Element& element);
    void buildChildren(dom::Element& element, LayoutBox& box);
    void collectAtomicInlines(dom::Element& element, LayoutBox& inlineBox,
                              std::vector<text::InlinePlaceholder>& placeholders);
    void measureAtomicInlines(LayoutBox& inlineBox);
    void applyStyles(LayoutBox& box);
    void transferFrames(LayoutBox& box, float parentX, float parentY);

    dom::Document& document_;
    std::unique_ptr<LayoutBox> root_;
    YGConfigRef config_ = nullptr;
    float viewportWidth_ = 0.0f;
    float viewportHeight_ = 0.0f;
    float devicePixelRatio_ = 1.0f;
    bool treeDirty_ = true;
};

} // namespace xgu::layout
