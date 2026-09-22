#include "paint/Painter.h"

#include "core/AssetLoader.h"
#include "core/Log.h"
#include "css/ComputedStyle.h"
#include "dom/Document.h"
#include "css/SelectorMatcher.h"
#include "dom/Element.h"
#include "dom/TextControl.h"
#include "layout/LayoutEngine.h"
#include "paint/BoxGeometry.h"
#include "paint/ImageCache.h"

#include <include/core/SkCanvas.h>
#include <include/core/SkColor.h>
#include <include/core/SkBlurTypes.h>
#include <include/core/SkMaskFilter.h>
#include <include/core/SkPaint.h>
#include <include/core/SkPath.h>
#include <include/core/SkPathBuilder.h>
#include <include/core/SkPictureRecorder.h>
#include <include/core/SkRRect.h>
#include <include/core/SkRect.h>
#include <include/core/SkSamplingOptions.h>
#include <include/core/SkTileMode.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace xgu::paint {
namespace {

using css::ComputedStyle;
using layout::LayoutBox;
using layout::Rect;

SkColor toSkColor(const css::Color& color) { return static_cast<SkColor>(color.toArgb()); }

} // namespace

Painter::Painter(dom::Document& document, layout::LayoutEngine& layout)
    : document_(document), layout_(layout) {}

render::DisplayList Painter::paint(int widthPx, int heightPx, float dpr, uint64_t frameId) {
    render::DisplayList list;
    if (widthPx <= 0 || heightPx <= 0 || dpr <= 0.0f || !layout_.root()) {
        return list;
    }
    SkPictureRecorder recorder;
    SkCanvas* canvas = recorder.beginRecording(SkRect::MakeIWH(widthPx, heightPx));
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->scale(dpr, dpr); // the tree below is in CSS pixels
    paintInto(*canvas);

    list.picture = recorder.finishRecordingAsPicture();
    list.dirtyPx = SkIRect::MakeWH(widthPx, heightPx);
    list.sizePx = SkISize::Make(widthPx, heightPx);
    list.devicePixelRatio = dpr;
    list.frameId = frameId;
    return list;
}

void Painter::paintInto(SkCanvas& canvas) {
    if (LayoutBox* root = layout_.root()) {
        paintStackingContext(canvas, *root);
    }
}

void Painter::paintStackingContext(SkCanvas& canvas, LayoutBox& box) {
    const ComputedStyle* style = box.style();
    if (!style || style->display == css::Display::None) {
        return;
    }

    const bool hasTransform = !style->transform.identity;
    const bool hasOpacity = style->opacity < 1.0f;
    SkAutoCanvasRestore restore(&canvas, hasTransform || hasOpacity);
    if (hasTransform) {
        canvas.concat(transformMatrix(box));
    }
    if (hasOpacity) {
        const SkRect bounds = toSkRect(box.borderBox());
        canvas.saveLayerAlphaf(&bounds, style->opacity);
    }
    paintBoxAndDescendants(canvas, box, true);
}

void Painter::paintBoxAndDescendants(SkCanvas& canvas, LayoutBox& box, bool isStackingContextRoot) {
    const ComputedStyle* style = box.style();
    if (!style || style->display == css::Display::None) {
        return;
    }
    (void)isStackingContextRoot;

    paintDecorations(canvas, box);

    // overflow: hidden clips every descendant (including out-of-flow ones, a
    // documented deviation).
    const bool clips = style->clipsOverflow();
    SkAutoCanvasRestore restore(&canvas, clips);
    if (clips) {
        canvas.clipRRect(paddingBoxRRect(box), true);
    }
    paintChildren(canvas, box);
}

void Painter::paintDecorations(SkCanvas& canvas, LayoutBox& box) {
    const ComputedStyle& style = *box.style();
    if (box.borderBox().isEmpty()) {
        return;
    }
    if (box.kind() == layout::BoxKind::InlineContext) {
        // Anonymous box: it only carries text. Its style pointer is the parent's,
        // so painting a background or a border here would draw them twice.
        return;
    }
    if (!isVisible(box)) {
        return; // visibility: hidden still lays out and still clips
    }

    paintShadows(canvas, box);

    if (!style.backgroundColor.isTransparent()) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(toSkColor(style.backgroundColor));
        canvas.drawRRect(borderBoxRRect(box), paint);
    }
    if (!style.backgroundImage.empty()) {
        paintBackgroundImage(canvas, box);
    }
    paintBorders(canvas, box);

    if (box.kind() == layout::BoxKind::Replaced) {
        paintReplaced(canvas, box);
    }
}

void Painter::paintTextControl(SkCanvas& canvas, LayoutBox& box, text::InlineContent& content, const Rect& frame,
                               bool beforeText) {
    // The control is the parent element of this anonymous inline box.
    LayoutBox* parent = box.parent();
    dom::Element* element = parent ? parent->element() : nullptr;
    dom::TextControl* control = element ? element->textControl() : nullptr;
    if (!control || !control->isEditable()) {
        return;
    }
    const css::ElementStateProvider* state = document_.elementStateProvider();
    if (!state || !state->isFocused(*element)) {
        return; // no caret and no selection unless the field has focus
    }

    if (beforeText) {
        // The selection highlight goes behind the glyphs.
        if (!control->hasSelection() || control->showingPlaceholder()) {
            return;
        }
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(SkColorSetARGB(0x66, 0x5A, 0xC8, 0xFA));
        for (const Rect& rect : content.rectsForRange(control->selectionStart(), control->selectionEnd())) {
            canvas.drawRect(SkRect::MakeXYWH(frame.x + rect.x, frame.y + rect.y, rect.width, rect.height), paint);
        }
        return;
    }

    // The caret goes on top, and only when nothing is selected.
    if (control->hasSelection()) {
        return;
    }
    const Rect caret = control->showingPlaceholder() ? content.caretRect(0) : content.caretRect(control->caret());
    SkPaint paint;
    paint.setColor(toSkColor(box.style()->color));
    canvas.drawRect(SkRect::MakeXYWH(frame.x + caret.x, frame.y + caret.y, std::max(1.0f, caret.width), caret.height),
                    paint);
}

void Painter::paintShadows(SkCanvas& canvas, LayoutBox& box) {
    const ComputedStyle& style = *box.style();
    if (style.boxShadow.empty()) {
        return;
    }
    const SkRRect borderRRect = borderBoxRRect(box);
    for (const css::BoxShadow& shadow : style.boxShadow) {
        if (shadow.inset || shadow.color.isTransparent()) {
            continue; // inset shadows are out of scope for the MVP
        }
        SkRRect shape = borderRRect;
        if (shadow.spread != 0.0f) {
            shape.outset(shadow.spread, shadow.spread);
        }
        shape.offset(shadow.offsetX, shadow.offsetY);

        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(toSkColor(shadow.color));
        if (shadow.blur > 0.0f) {
            // CSS blur radius is roughly two standard deviations.
            paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, shadow.blur / 2.0f));
        }
        // The shadow is not drawn under the box itself.
        SkAutoCanvasRestore restore(&canvas, true);
        canvas.clipRRect(borderRRect, SkClipOp::kDifference, true);
        canvas.drawRRect(shape, paint);
    }
}

void Painter::paintBackgroundImage(SkCanvas& canvas, LayoutBox& box) {
    const ComputedStyle& style = *box.style();
    IAssetLoader* loader = document_.assetLoader();
    if (!loader) {
        return;
    }
    sk_sp<SkImage> image = ImageCache::instance().get(*loader, document_.url(), style.backgroundImage);
    if (!image) {
        return;
    }

    const Rect padding = box.paddingBox();
    if (padding.isEmpty()) {
        return;
    }
    const float imageWidth = static_cast<float>(image->width());
    const float imageHeight = static_cast<float>(image->height());
    if (imageWidth <= 0.0f || imageHeight <= 0.0f) {
        return;
    }

    // background-size
    float drawWidth = imageWidth;
    float drawHeight = imageHeight;
    switch (style.backgroundSize.kind) {
    case css::BackgroundSizeKind::Cover:
    case css::BackgroundSizeKind::Contain: {
        const float scaleX = padding.width / imageWidth;
        const float scaleY = padding.height / imageHeight;
        const float scale = style.backgroundSize.kind == css::BackgroundSizeKind::Cover ? std::max(scaleX, scaleY)
                                                                                       : std::min(scaleX, scaleY);
        drawWidth = imageWidth * scale;
        drawHeight = imageHeight * scale;
        break;
    }
    case css::BackgroundSizeKind::Explicit: {
        css::LengthContext context;
        context.fontSize = style.fontSize;
        context.rootFontSize = style.fontSize;
        const css::Length& width = style.backgroundSize.width;
        const css::Length& height = style.backgroundSize.height;
        drawWidth = width.isPercent() ? width.value / 100.0f * padding.width
                                      : css::resolveLength(width, context, imageWidth);
        if (height.isAuto()) {
            drawHeight = drawWidth * imageHeight / imageWidth;
        } else {
            drawHeight = height.isPercent() ? height.value / 100.0f * padding.height
                                            : css::resolveLength(height, context, imageHeight);
        }
        break;
    }
    case css::BackgroundSizeKind::Auto:
        break;
    }
    if (drawWidth <= 0.0f || drawHeight <= 0.0f) {
        return;
    }

    // background-position
    const auto position = [&](size_t index, float extent, float imageExtent) {
        const css::Length& length = style.backgroundPosition[index];
        if (length.isPercent()) {
            return length.value / 100.0f * (extent - imageExtent);
        }
        css::LengthContext context;
        context.fontSize = style.fontSize;
        context.rootFontSize = style.fontSize;
        return css::resolveLength(length, context, 0.0f);
    };
    const float x = padding.x + position(0, padding.width, drawWidth);
    const float y = padding.y + position(1, padding.height, drawHeight);

    SkAutoCanvasRestore restore(&canvas, true);
    canvas.clipRRect(paddingBoxRRect(box), true);

    const SkSamplingOptions sampling(SkFilterMode::kLinear, SkMipmapMode::kNone);
    if (style.backgroundRepeat == css::BackgroundRepeat::NoRepeat) {
        canvas.drawImageRect(image, SkRect::MakeXYWH(x, y, drawWidth, drawHeight), sampling);
        return;
    }
    // Tiling: a shader repeats the scaled image across the padding box.
    const SkTileMode tileX = (style.backgroundRepeat == css::BackgroundRepeat::Repeat ||
                              style.backgroundRepeat == css::BackgroundRepeat::RepeatX)
                                 ? SkTileMode::kRepeat
                                 : SkTileMode::kDecal;
    const SkTileMode tileY = (style.backgroundRepeat == css::BackgroundRepeat::Repeat ||
                              style.backgroundRepeat == css::BackgroundRepeat::RepeatY)
                                 ? SkTileMode::kRepeat
                                 : SkTileMode::kDecal;
    SkMatrix local = SkMatrix::Translate(x, y);
    local.preScale(drawWidth / imageWidth, drawHeight / imageHeight);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setShader(image->makeShader(tileX, tileY, sampling, &local));
    canvas.drawRect(toSkRect(padding), paint);
}

void Painter::paintBorders(SkCanvas& canvas, LayoutBox& box) {
    const ComputedStyle& style = *box.style();
    if (!style.hasVisibleBorder()) {
        return;
    }
    const float widths[4] = {box.borderEdge(css::kTop), box.borderEdge(css::kRight), box.borderEdge(css::kBottom),
                             box.borderEdge(css::kLeft)};

    const bool uniformColor = style.borderColor[0] == style.borderColor[1] &&
                              style.borderColor[1] == style.borderColor[2] &&
                              style.borderColor[2] == style.borderColor[3];
    const bool uniformWidth = widths[0] == widths[1] && widths[1] == widths[2] && widths[2] == widths[3];

    if (uniformColor && uniformWidth) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(toSkColor(style.borderColor[0]));
        canvas.drawDRRect(borderBoxRRect(box), paddingBoxRRect(box), paint);
        return;
    }

    // Per-side borders: drawn as trapezoids so adjacent colours meet on the
    // diagonal, the way browsers do it. Radii are ignored in this path.
    const Rect& frame = box.borderBox();
    const float left = frame.x;
    const float top = frame.y;
    const float right = frame.right();
    const float bottom = frame.bottom();
    const float innerLeft = left + widths[css::kLeft];
    const float innerTop = top + widths[css::kTop];
    const float innerRight = right - widths[css::kRight];
    const float innerBottom = bottom - widths[css::kBottom];

    const auto drawSide = [&](css::Side side, const SkPoint points[4]) {
        if (widths[side] <= 0.0f || style.borderColor[side].isTransparent() ||
            style.borderStyle[side] == css::BorderStyle::None ||
            style.borderStyle[side] == css::BorderStyle::Hidden) {
            return;
        }
        SkPathBuilder builder;
        builder.moveTo(points[0]);
        builder.lineTo(points[1]);
        builder.lineTo(points[2]);
        builder.lineTo(points[3]);
        builder.close();
        const SkPath path = builder.detach();
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(toSkColor(style.borderColor[side]));
        canvas.drawPath(path, paint);
    };

    const SkPoint topSide[4] = {{left, top}, {right, top}, {innerRight, innerTop}, {innerLeft, innerTop}};
    const SkPoint rightSide[4] = {{right, top}, {right, bottom}, {innerRight, innerBottom}, {innerRight, innerTop}};
    const SkPoint bottomSide[4] = {
        {right, bottom}, {left, bottom}, {innerLeft, innerBottom}, {innerRight, innerBottom}};
    const SkPoint leftSide[4] = {{left, bottom}, {left, top}, {innerLeft, innerTop}, {innerLeft, innerBottom}};
    drawSide(css::kTop, topSide);
    drawSide(css::kRight, rightSide);
    drawSide(css::kBottom, bottomSide);
    drawSide(css::kLeft, leftSide);
}

void Painter::paintReplaced(SkCanvas& canvas, LayoutBox& box) {
    dom::Element* element = box.element();
    IAssetLoader* loader = document_.assetLoader();
    if (!element || !loader) {
        return;
    }
    const std::string source = element->getAttributeOrEmpty(Atom("src"));
    if (source.empty()) {
        return;
    }
    sk_sp<SkImage> image = ImageCache::instance().get(*loader, document_.url(), source);
    if (!image) {
        return;
    }
    const Rect content = box.contentBox();
    if (content.isEmpty()) {
        return;
    }
    SkAutoCanvasRestore restore(&canvas, box.style()->hasBorderRadius());
    if (box.style()->hasBorderRadius()) {
        canvas.clipRRect(paddingBoxRRect(box), true);
    }
    canvas.drawImageRect(image, toSkRect(content), SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone));
}

void Painter::paintChildren(SkCanvas& canvas, LayoutBox& box) {
    // Buckets in CSS 2.1 Appendix E order.
    std::vector<LayoutBox*> negative;
    std::vector<LayoutBox*> inFlow;
    std::vector<LayoutBox*> positioned;
    std::vector<LayoutBox*> positive;

    const auto bucket = [&](LayoutBox* child) {
        if (!child->style() || child->style()->display == css::Display::None) {
            return;
        }
        if (createsStackingContext(*child)) {
            const int order = stackingOrder(*child);
            if (order < 0) {
                negative.push_back(child);
            } else if (order > 0) {
                positive.push_back(child);
            } else {
                positioned.push_back(child);
            }
            return;
        }
        if (isPositioned(*child)) {
            positioned.push_back(child);
            return;
        }
        inFlow.push_back(child);
    };

    for (const std::unique_ptr<LayoutBox>& child : box.children()) {
        bucket(child.get());
    }

    const auto byOrder = [](const LayoutBox* a, const LayoutBox* b) {
        return stackingOrder(*a) < stackingOrder(*b);
    };
    std::stable_sort(negative.begin(), negative.end(), byOrder);
    std::stable_sort(positive.begin(), positive.end(), byOrder);

    const auto paintChild = [&](LayoutBox* child) {
        if (createsStackingContext(*child)) {
            paintStackingContext(canvas, *child);
        } else {
            paintBoxAndDescendants(canvas, *child, false);
        }
    };

    for (LayoutBox* child : negative) {
        paintChild(child);
    }
    for (LayoutBox* child : inFlow) {
        paintChild(child);
    }

    // Inline content of this box, then the atomic inlines sitting in the text.
    if (text::InlineContent* content = box.inlineContent()) {
        if (isVisible(box)) {
            // The paragraph origin is the content box: that is the width it was
            // laid out at, and what the placeholder rects are relative to.
            const Rect frame = box.contentBox();
            paintTextControl(canvas, box, *content, frame, true);
            content->paint(&canvas, frame.x, frame.y);
            paintTextControl(canvas, box, *content, frame, false);
        }
        for (const std::unique_ptr<LayoutBox>& atomic : box.atomicInlines()) {
            paintChild(atomic.get());
        }
    }

    for (LayoutBox* child : positioned) {
        paintChild(child);
    }
    for (LayoutBox* child : positive) {
        paintChild(child);
    }
}

} // namespace xgu::paint
