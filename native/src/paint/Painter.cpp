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
#include <include/effects/SkGradient.h>

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

render::DisplayList Painter::paint(int widthPx, int heightPx, float dpr, uint64_t frameId, bool fullDamage) {
    render::DisplayList list;
    if (widthPx <= 0 || heightPx <= 0 || dpr <= 0.0f || !layout_.root()) {
        return list;
    }
    damage_ = SkIRect::MakeEmpty();
    // A rebuilt box tree loses the boxes that went away, and their pixels are
    // still on the surface, so nothing per-box can be trusted that frame.
    damageEverything_ = fullDamage || layout_.rebuiltTree();

    SkPictureRecorder recorder;
    SkCanvas* canvas = recorder.beginRecording(SkRect::MakeIWH(widthPx, heightPx));
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->scale(dpr, dpr); // the tree below is in CSS pixels
    paintInto(*canvas);

    list.picture = recorder.finishRecordingAsPicture();
    const SkIRect whole = SkIRect::MakeWH(widthPx, heightPx);
    if (damageEverything_) {
        list.dirtyPx = whole;
    } else {
        // One pixel of slack absorbs the rounding of anti-aliased edges.
        damage_.outset(1, 1);
        if (!damage_.intersect(whole)) {
            damage_ = SkIRect::MakeEmpty();
        }
        list.dirtyPx = damage_;
    }
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
    {
        // Scrolling is a translation of the content inside the clip, so nothing
        // below has to know the box scrolls.
        const bool scrolled = box.scrollLeft() != 0.0f || box.scrollTop() != 0.0f;
        SkAutoCanvasRestore scrollRestore(&canvas, scrolled);
        if (scrolled) {
            canvas.translate(-box.scrollLeft(), -box.scrollTop());
        }
        paintChildren(canvas, box);
    }
    if (clips) {
        // The bars sit on top of the content and do not scroll with it.
        paintScrollbars(canvas, box);
    }
}

void Painter::paintScrollbars(SkCanvas& canvas, LayoutBox& box) {
    if (!box.scrollsHorizontally() && !box.scrollsVertically()) {
        return;
    }
    const Rect padding = box.paddingBox();
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SkColorSetARGB(0x80, 0xC0, 0xC8, 0xD4));

    // One plain thumb, no track and no buttons. A game UI that wants a styled
    // scrollbar builds it out of elements; this is only so overflow is visible.
    constexpr float kThickness = 4.0f;
    constexpr float kMinThumb = 16.0f;
    if (box.scrollsVertically()) {
        const float ratio = padding.height / box.scrollHeight();
        const float thumb = std::max(kMinThumb, padding.height * ratio);
        const float travel = padding.height - thumb;
        const float offset = box.maxScrollTop() > 0.0f ? travel * (box.scrollTop() / box.maxScrollTop()) : 0.0f;
        canvas.drawRoundRect(
            SkRect::MakeXYWH(padding.right() - kThickness - 1.0f, padding.y + offset, kThickness, thumb),
            kThickness * 0.5f, kThickness * 0.5f, paint);
    }
    if (box.scrollsHorizontally()) {
        const float ratio = padding.width / box.scrollWidth();
        const float thumb = std::max(kMinThumb, padding.width * ratio);
        const float travel = padding.width - thumb;
        const float offset = box.maxScrollLeft() > 0.0f ? travel * (box.scrollLeft() / box.maxScrollLeft()) : 0.0f;
        canvas.drawRoundRect(
            SkRect::MakeXYWH(padding.x + offset, padding.bottom() - kThickness - 1.0f, thumb, kThickness),
            kThickness * 0.5f, kThickness * 0.5f, paint);
    }
}

void Painter::trackDamage(SkCanvas& canvas, LayoutBox& box) {
    // The bounds and the dirty bits are recorded even on a frame that redraws
    // everything: without that, the next frame would see every box as brand new
    // and damage the whole surface again.
    const ComputedStyle* style = box.style();
    if (!style) {
        return;
    }

    // What the box can touch: its border box, grown by the reach of its shadows.
    SkRect local = toSkRect(box.borderBox());
    for (const css::BoxShadow& shadow : style->boxShadow) {
        if (shadow.inset) {
            continue;
        }
        SkRect shape = toSkRect(box.borderBox());
        shape.outset(shadow.spread + shadow.blur, shadow.spread + shadow.blur);
        shape.offset(shadow.offsetX, shadow.offsetY);
        local.join(shape);
    }

    // The canvas already carries the device scale, every ancestor transform and
    // every clip, so mapping through it gives exactly where this lands.
    SkRect mapped = canvas.getTotalMatrix().mapRect(local);
    SkIRect device = mapped.roundOut();
    if (!device.intersect(canvas.getDeviceClipBounds())) {
        device = SkIRect::MakeEmpty();
    }

    const layout::Rect bounds{static_cast<float>(device.left()), static_cast<float>(device.top()),
                              static_cast<float>(device.width()), static_cast<float>(device.height())};
    const bool moved = !box.hasPaintedBefore() || !(bounds == box.lastPaintedBounds());
    bool repaint = false;
    if (dom::Element* element = box.element()) {
        repaint = (element->dirtyBits() & (dom::kDirtyPaintSelf | dom::kDirtyPaintChildren)) != 0;
        // Painting is what these bits were asking for, so they are answered now.
        // Nothing else reads them, and leaving them set would mark every element
        // dirty for ever and damage the whole surface on every frame.
        element->clearDirty(dom::kDirtyPaintSelf | dom::kDirtyPaintChildren);
    }
    if ((moved || repaint) && !damageEverything_) {
        if (box.hasPaintedBefore()) {
            // Both where it was and where it is now have to be redrawn.
            const layout::Rect& before = box.lastPaintedBounds();
            damage_.join(SkIRect::MakeXYWH(static_cast<int>(before.x), static_cast<int>(before.y),
                                           static_cast<int>(before.width), static_cast<int>(before.height)));
        }
        damage_.join(device);
    }
    box.setLastPaintedBounds(bounds);
    box.markPainted();
}

void Painter::paintDecorations(SkCanvas& canvas, LayoutBox& box) {
    const ComputedStyle& style = *box.style();
    trackDamage(canvas, box);
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
    if (style.backgroundGradient.valid()) {
        paintBackgroundGradient(canvas, box);
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

void Painter::paintBackgroundGradient(SkCanvas& canvas, LayoutBox& box) {
    const css::Gradient& gradient = box.style()->backgroundGradient;
    const Rect frame = box.paddingBox();
    if (frame.isEmpty()) {
        return;
    }

    std::vector<SkColor4f> colors;
    std::vector<float> positions;
    colors.reserve(gradient.stops.size());
    positions.reserve(gradient.stops.size());
    for (const css::GradientStop& stop : gradient.stops) {
        colors.push_back(SkColor4f::FromColor(toSkColor(stop.color)));
        positions.push_back(std::clamp(stop.position, 0.0f, 1.0f));
    }

    const SkGradient description(SkGradient::Colors(SkSpan<const SkColor4f>(colors),
                                                    SkSpan<const float>(positions), SkTileMode::kClamp),
                                 SkGradient::Interpolation{});
    sk_sp<SkShader> shader;
    if (gradient.kind == css::GradientKind::Radial) {
        const SkPoint centre = SkPoint::Make(frame.x + frame.width * 0.5f, frame.y + frame.height * 0.5f);
        // farthest-corner, which is what an unqualified radial-gradient means.
        const float radius = std::hypot(frame.width, frame.height) * 0.5f;
        shader = SkShaders::RadialGradient(centre, radius, description);
    } else {
        // The CSS gradient line runs through the centre at `angle`, with 0deg
        // pointing up and growing clockwise, and is long enough that the corners
        // land exactly on its ends.
        const float radians = gradient.angleDegrees * 3.14159265358979323846f / 180.0f;
        const float dirX = std::sin(radians);
        const float dirY = -std::cos(radians);
        const float halfLength =
            (std::fabs(frame.width * dirX) + std::fabs(frame.height * dirY)) * 0.5f;
        const SkPoint centre = SkPoint::Make(frame.x + frame.width * 0.5f, frame.y + frame.height * 0.5f);
        const SkPoint ends[2] = {
            SkPoint::Make(centre.fX - dirX * halfLength, centre.fY - dirY * halfLength),
            SkPoint::Make(centre.fX + dirX * halfLength, centre.fY + dirY * halfLength),
        };
        shader = SkShaders::LinearGradient(ends, description);
    }
    if (!shader) {
        return;
    }
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setShader(std::move(shader));
    // The gradient paints over the padding box, inside the border, as CSS says
    // for the default background-origin.
    canvas.drawRRect(paddingBoxRRect(box), paint);
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
