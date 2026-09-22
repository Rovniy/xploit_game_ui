#pragma once

#include "render/DisplayList.h"

#include <cstdint>

class SkCanvas;

namespace xgu::dom {
class Document;
}

namespace xgu::layout {
class LayoutBox;
class LayoutEngine;
struct Rect;
} // namespace xgu::layout

namespace xgu::text {
class InlineContent;
}

namespace xgu::paint {

// Turns the laid-out box tree into a display list.
//
// Paint order follows a simplified CSS 2.1 Appendix E: a stacking context draws
// its own decorations, then negative z-index contexts, in-flow descendants,
// inline content, positioned descendants with z-index auto/0, and finally
// positive z-index contexts. What the MVP leaves out is listed in
// docs/css-support.md (no per-box picture caching, no dirty regions, no inset
// shadows or gradients).
class Painter {
public:
    Painter(dom::Document& document, layout::LayoutEngine& layout);

    // Records the whole viewport. `widthPx`/`heightPx` are device pixels and
    // `dpr` converts the CSS pixels the layout works in. The display list also
    // carries the region that changed since the previous frame, so the provider
    // only has to rasterise that much; `fullDamage` forces the whole surface,
    // which a resize or a new surface needs.
    render::DisplayList paint(int widthPx, int heightPx, float dpr, uint64_t frameId, bool fullDamage = false);

    // Paints into a canvas the caller owns (golden tests, the CLI).
    void paintInto(SkCanvas& canvas);

private:
    void paintStackingContext(SkCanvas& canvas, layout::LayoutBox& box);
    void paintBoxAndDescendants(SkCanvas& canvas, layout::LayoutBox& box, bool isStackingContextRoot);
    void paintDecorations(SkCanvas& canvas, layout::LayoutBox& box);
    void paintBackgroundGradient(SkCanvas& canvas, layout::LayoutBox& box);
    void paintBackgroundImage(SkCanvas& canvas, layout::LayoutBox& box);
    void paintBorders(SkCanvas& canvas, layout::LayoutBox& box);
    void paintShadows(SkCanvas& canvas, layout::LayoutBox& box);
    void paintReplaced(SkCanvas& canvas, layout::LayoutBox& box);
    void paintChildren(SkCanvas& canvas, layout::LayoutBox& box);
    // Notes where a box landed this frame and adds it to the damage when that
    // differs from where it was, or when its element asked for a repaint.
    void trackDamage(SkCanvas& canvas, layout::LayoutBox& box);
    // A thin thumb for each axis that actually overflows.
    void paintScrollbars(SkCanvas& canvas, layout::LayoutBox& box);
    // Selection highlight (before the glyphs) and caret (after them) for a
    // focused <input> or <textarea>.
    void paintTextControl(SkCanvas& canvas, layout::LayoutBox& box, text::InlineContent& content,
                          const layout::Rect& frame, bool beforeText);

    dom::Document& document_;
    layout::LayoutEngine& layout_;
    // Union of what changed this frame, in device pixels.
    SkIRect damage_ = SkIRect::MakeEmpty();
    bool damageEverything_ = true;
};

} // namespace xgu::paint
