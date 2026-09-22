#include "layout/LayoutEngine.h"

#include "core/Log.h"
#include "core/AssetLoader.h"
#include "dom/Document.h"
#include "dom/Element.h"
#include "dom/TextControl.h"
#include "paint/ImageCache.h"
#include "text/FontManager.h"

#include <yoga/YGConfig.h>
#include <yoga/YGNode.h>
#include <yoga/YGNodeLayout.h>
#include <yoga/YGNodeStyle.h>

#include <algorithm>
#include <cmath>

namespace xgu::layout {
namespace {

using css::ComputedStyle;
using css::Length;
using css::LengthUnit;

constexpr YGEdge kEdges[4] = {YGEdgeTop, YGEdgeRight, YGEdgeBottom, YGEdgeLeft};

// Applies a CSS length to a Yoga setter trio (points / percent / auto).
template <typename SetPoint, typename SetPercent, typename SetAuto>
void applyLength(const Length& length, float fontSize, float rootFontSize, float viewportWidth,
                 float viewportHeight, SetPoint setPoint, SetPercent setPercent, SetAuto setAuto) {
    switch (length.unit) {
    case LengthUnit::Auto:
        setAuto();
        return;
    case LengthUnit::None:
        setPoint(YGUndefined);
        return;
    case LengthUnit::Percent:
        setPercent(length.value);
        return;
    default: {
        css::LengthContext context;
        context.fontSize = fontSize;
        context.rootFontSize = rootFontSize;
        context.viewportWidth = viewportWidth;
        context.viewportHeight = viewportHeight;
        setPoint(css::resolveLength(length, context, 0.0f));
        return;
    }
    }
}

YGFlexDirection toYogaDirection(const ComputedStyle& style) {
    if (!style.isFlexContainer()) {
        // Block containers stack their children vertically.
        return YGFlexDirectionColumn;
    }
    switch (style.flexDirection) {
    case css::FlexDirection::Row:
        return YGFlexDirectionRow;
    case css::FlexDirection::RowReverse:
        return YGFlexDirectionRowReverse;
    case css::FlexDirection::Column:
        return YGFlexDirectionColumn;
    case css::FlexDirection::ColumnReverse:
        return YGFlexDirectionColumnReverse;
    }
    return YGFlexDirectionColumn;
}

YGJustify toYogaJustify(css::Justify justify) {
    switch (justify) {
    case css::Justify::FlexEnd:
        return YGJustifyFlexEnd;
    case css::Justify::Center:
        return YGJustifyCenter;
    case css::Justify::SpaceBetween:
        return YGJustifySpaceBetween;
    case css::Justify::SpaceAround:
        return YGJustifySpaceAround;
    case css::Justify::SpaceEvenly:
        return YGJustifySpaceEvenly;
    case css::Justify::FlexStart:
        break;
    }
    return YGJustifyFlexStart;
}

YGAlign toYogaAlign(css::Align align, YGAlign fallback) {
    switch (align) {
    case css::Align::FlexStart:
        return YGAlignFlexStart;
    case css::Align::FlexEnd:
        return YGAlignFlexEnd;
    case css::Align::Center:
        return YGAlignCenter;
    case css::Align::Stretch:
        return YGAlignStretch;
    case css::Align::Baseline:
        return YGAlignBaseline;
    case css::Align::SpaceBetween:
        return YGAlignSpaceBetween;
    case css::Align::SpaceAround:
        return YGAlignSpaceAround;
    case css::Align::SpaceEvenly:
        return YGAlignSpaceEvenly;
    case css::Align::Auto:
        break;
    }
    return fallback;
}

YGPositionType toYogaPosition(css::PositionType position) {
    switch (position) {
    case css::PositionType::Relative:
        return YGPositionTypeRelative;
    case css::PositionType::Absolute:
    case css::PositionType::Fixed:
        return YGPositionTypeAbsolute;
    case css::PositionType::Static:
        break;
    }
    return YGPositionTypeStatic;
}

YGOverflow toYogaOverflow(css::Overflow overflow) {
    switch (overflow) {
    case css::Overflow::Hidden:
        return YGOverflowHidden;
    case css::Overflow::Scroll:
    case css::Overflow::Auto:
        return YGOverflowScroll;
    case css::Overflow::Visible:
        break;
    }
    return YGOverflowVisible;
}

// Measure callback for inline formatting contexts and replaced boxes.
YGSize measureBox(YGNodeConstRef node, float width, YGMeasureMode widthMode, float height,
                  YGMeasureMode heightMode) {
    auto* box = static_cast<LayoutBox*>(YGNodeGetContext(node));
    if (!box) {
        return YGSize{0.0f, 0.0f};
    }

    if (box->kind() == BoxKind::Replaced) {
        float resultWidth = box->intrinsicWidth();
        float resultHeight = box->intrinsicHeight();
        const float aspect = resultHeight > 0.0f ? resultWidth / resultHeight : 1.0f;
        if (widthMode == YGMeasureModeExactly) {
            resultWidth = width;
            if (heightMode != YGMeasureModeExactly && aspect > 0.0f) {
                resultHeight = width / aspect;
            }
        } else if (widthMode == YGMeasureModeAtMost) {
            resultWidth = std::min(resultWidth, width);
        }
        if (heightMode == YGMeasureModeExactly) {
            resultHeight = height;
        } else if (heightMode == YGMeasureModeAtMost) {
            resultHeight = std::min(resultHeight, height);
        }
        return YGSize{resultWidth, resultHeight};
    }

    text::InlineContent* content = box->inlineContent();
    if (!content || content->empty()) {
        return YGSize{0.0f, 0.0f};
    }
    float available = width;
    if (widthMode == YGMeasureModeUndefined || std::isnan(available)) {
        available = 1.0e6f;
    }
    const text::InlineContent::Size size = content->layout(available);
    float resultWidth = widthMode == YGMeasureModeExactly ? width : std::min(size.width, available);
    float resultHeight = size.height;
    if (heightMode == YGMeasureModeExactly) {
        resultHeight = height;
    }
    return YGSize{resultWidth, resultHeight};
}

float baselineBox(YGNodeConstRef node, float width, float height) {
    auto* box = static_cast<LayoutBox*>(YGNodeGetContext(node));
    if (!box || !box->inlineContent()) {
        return height;
    }
    (void)width;
    const float baseline = box->inlineContent()->firstBaseline();
    return baseline > 0.0f ? baseline : height;
}

// True when the element's children are all inline-level (so the block gets one
// anonymous inline formatting context instead of child boxes).
bool hasOnlyInlineContent(const dom::Element& element) {
    bool sawSomething = false;
    for (size_t i = 0; i < element.childCount(); ++i) {
        const dom::Node* child = element.childAt(i);
        if (child->isComment()) {
            continue;
        }
        if (child->isText()) {
            sawSomething = true;
            continue;
        }
        if (!child->isElement()) {
            continue;
        }
        const css::ComputedStyle* style = static_cast<const dom::Element*>(child)->computedStyle();
        if (!style || style->display == css::Display::None) {
            continue;
        }
        if (!style->isInlineLevel()) {
            return false;
        }
        sawSomething = true;
    }
    return sawSomething;
}

bool isReplaced(const dom::Element& element) { return element.knownTag() == html::HtmlTag::Img; }

} // namespace

LayoutEngine::LayoutEngine(dom::Document& document) : document_(document) {
    config_ = YGConfigNew();
    YGConfigSetUseWebDefaults(config_, true);
    YGConfigSetPointScaleFactor(config_, 1.0f);
}

LayoutEngine::~LayoutEngine() {
    root_.reset();
    if (config_) {
        YGConfigFree(config_);
        config_ = nullptr;
    }
}

std::unique_ptr<LayoutBox> LayoutEngine::buildBox(dom::Element& element) {
    const css::ComputedStyle* style = element.computedStyle();
    if (!style || style->display == css::Display::None) {
        return nullptr;
    }

    BoxKind kind = BoxKind::Block;
    if (isReplaced(element)) {
        kind = BoxKind::Replaced;
    } else if (style->isFlexContainer()) {
        kind = BoxKind::Flex;
    }

    auto box = std::make_unique<LayoutBox>(kind, &element, style);
    if (kind == BoxKind::Replaced) {
        // The width/height attributes win; otherwise the decoded image decides.
        float width = 0.0f;
        float height = 0.0f;
        if (const std::string* value = element.getAttribute(Atom("width"))) {
            width = static_cast<float>(std::atof(value->c_str()));
        }
        if (const std::string* value = element.getAttribute(Atom("height"))) {
            height = static_cast<float>(std::atof(value->c_str()));
        }
        if ((width <= 0.0f || height <= 0.0f) && document_.assetLoader()) {
            const std::string source = element.getAttributeOrEmpty(Atom("src"));
            if (!source.empty()) {
                if (sk_sp<SkImage> image =
                        paint::ImageCache::instance().get(*document_.assetLoader(), document_.url(), source)) {
                    const float intrinsicWidth = static_cast<float>(image->width());
                    const float intrinsicHeight = static_cast<float>(image->height());
                    if (width > 0.0f && height <= 0.0f) {
                        height = width * intrinsicHeight / std::max(1.0f, intrinsicWidth);
                    } else if (height > 0.0f && width <= 0.0f) {
                        width = height * intrinsicWidth / std::max(1.0f, intrinsicHeight);
                    } else {
                        width = intrinsicWidth;
                        height = intrinsicHeight;
                    }
                }
            }
        }
        box->setIntrinsicSize(width, height);
        YGNodeSetMeasureFunc(box->yogaNode(), &measureBox);
        return box;
    }

    buildChildren(element, *box);
    return box;
}

void LayoutEngine::buildChildren(dom::Element& element, LayoutBox& box) {
    const css::ComputedStyle* style = element.computedStyle();
    if (!style) {
        return;
    }

    if (element.isTextControl()) {
        // A text control shows its own value, not DOM children. Its box is an
        // inline formatting context built from that value (or the placeholder).
        auto inlineBox = std::make_unique<LayoutBox>(BoxKind::InlineContext, nullptr, style);
        YGNodeSetNodeType(inlineBox->yogaNode(), YGNodeTypeText);
        YGNodeSetMeasureFunc(inlineBox->yogaNode(), &measureBox);
        YGNodeSetBaselineFunc(inlineBox->yogaNode(), &baselineBox);
        dom::TextControl& control = element.ensureTextControl();
        inlineBox->ensureInlineContent().buildLiteral(*style, dom::utf16ToUtf8(control.displayText()));
        box.addChild(std::move(inlineBox));
        return;
    }

    if (hasOnlyInlineContent(element)) {
        // One anonymous box carrying the whole inline formatting context.
        auto inlineBox = std::make_unique<LayoutBox>(BoxKind::InlineContext, nullptr, style);
        YGNodeSetNodeType(inlineBox->yogaNode(), YGNodeTypeText);
        YGNodeSetMeasureFunc(inlineBox->yogaNode(), &measureBox);
        YGNodeSetBaselineFunc(inlineBox->yogaNode(), &baselineBox);

        // Atomic inlines (<img>, inline-block, button, input) become placeholders
        // in the paragraph; their boxes hang off the inline box, not off Yoga.
        std::vector<text::InlinePlaceholder> placeholders;
        collectAtomicInlines(element, *inlineBox, placeholders);
        inlineBox->ensureInlineContent().build(element, *style, std::move(placeholders));

        if (!inlineBox->inlineContent()->empty()) {
            box.addChild(std::move(inlineBox));
        }
        return;
    }

    for (dom::Element* child : element.childElements()) {
        const css::ComputedStyle* childStyle = child->computedStyle();
        if (!childStyle || childStyle->display == css::Display::None) {
            continue;
        }
        if (childStyle->display == css::Display::Contents) {
            buildChildren(*child, box); // the element itself generates no box
            continue;
        }
        if (std::unique_ptr<LayoutBox> childBox = buildBox(*child)) {
            box.addChild(std::move(childBox));
        }
    }
}

void LayoutEngine::collectAtomicInlines(dom::Element& element, LayoutBox& inlineBox,
                                        std::vector<text::InlinePlaceholder>& placeholders) {
    for (dom::Element* child : element.childElements()) {
        const css::ComputedStyle* childStyle = child->computedStyle();
        if (!childStyle || childStyle->display == css::Display::None) {
            continue;
        }
        if (childStyle->display == css::Display::Inline) {
            collectAtomicInlines(*child, inlineBox, placeholders); // keep descending
            continue;
        }
        std::unique_ptr<LayoutBox> childBox = buildBox(*child);
        if (!childBox) {
            continue;
        }
        LayoutBox& added = inlineBox.addAtomicInline(std::move(childBox));
        text::InlinePlaceholder placeholder;
        placeholder.box = &added;
        placeholders.push_back(placeholder);
    }
}

// Lays an atomic inline out on its own so the paragraph knows how much room to
// reserve for it.
void LayoutEngine::measureAtomicInlines(LayoutBox& inlineBox) {
    text::InlineContent* content = inlineBox.inlineContent();
    if (!content || inlineBox.atomicInlines().empty()) {
        return;
    }
    std::vector<text::InlinePlaceholder> sizes;
    sizes.reserve(inlineBox.atomicInlines().size());
    for (const std::unique_ptr<LayoutBox>& child : inlineBox.atomicInlines()) {
        applyStyles(*child);
        YGNodeCalculateLayout(child->yogaNode(), YGUndefined, YGUndefined, YGDirectionLTR);
        text::InlinePlaceholder placeholder;
        placeholder.box = child.get();
        placeholder.width = YGNodeLayoutGetWidth(child->yogaNode());
        placeholder.height = YGNodeLayoutGetHeight(child->yogaNode());
        sizes.push_back(placeholder);
    }
    content->setPlaceholderSizes(sizes);
}

void LayoutEngine::applyStyles(LayoutBox& box) {
    // A restyle hands the element a new ComputedStyle, so the pointer captured
    // when the tree was built is stale (and, once the old style is released,
    // dangling). Refresh it before reading anything off it. Parents are visited
    // before their children, so an anonymous box sees the new parent style here.
    if (dom::Element* element = box.element()) {
        if (const ComputedStyle* current = element->computedStyle()) {
            box.setStyle(current);
        }
    } else if (const LayoutBox* parent = box.parent()) {
        box.setStyle(parent->style());
    }

    const ComputedStyle* style = box.style();
    YGNodeRef node = box.yogaNode();
    if (!style || !node) {
        return;
    }

    if (box.kind() == BoxKind::InlineContext) {
        // An anonymous inline formatting context borrows its parent's style only
        // for the inherited text properties. The box model belongs to the parent
        // element, so applying it here would inset (and size) the text twice.
        YGNodeStyleSetDisplay(node, YGDisplayFlex);
        YGNodeStyleSetPositionType(node, YGPositionTypeStatic);
        YGNodeStyleSetFlexGrow(node, 0.0f);
        YGNodeStyleSetFlexShrink(node, 0.0f);
        YGNodeStyleSetAlignSelf(node, YGAlignAuto);
        rebuildInlineContentIfRestyled(box);
        measureAtomicInlines(box);
        YGNodeMarkDirty(node);
        return;
    }

    const float fontSize = style->fontSize;
    const float rootFontSize = document_.documentElement() && document_.documentElement()->computedStyle()
                                   ? document_.documentElement()->computedStyle()->fontSize
                                   : 16.0f;
    const auto length = [&](const Length& value, auto setPoint, auto setPercent, auto setAuto) {
        applyLength(value, fontSize, rootFontSize, viewportWidth_, viewportHeight_, setPoint, setPercent, setAuto);
    };

    YGNodeStyleSetDisplay(node, style->display == css::Display::None ? YGDisplayNone : YGDisplayFlex);
    YGNodeStyleSetFlexDirection(node, toYogaDirection(*style));
    YGNodeStyleSetPositionType(node, toYogaPosition(style->position));
    YGNodeStyleSetOverflow(node, toYogaOverflow(style->overflowX));
    YGNodeStyleSetBoxSizing(node, style->boxSizing == css::BoxSizing::BorderBox ? YGBoxSizingBorderBox
                                                                                : YGBoxSizingContentBox);

    if (style->isFlexContainer()) {
        YGNodeStyleSetFlexWrap(node, style->flexWrap == css::FlexWrap::Wrap          ? YGWrapWrap
                                     : style->flexWrap == css::FlexWrap::WrapReverse ? YGWrapWrapReverse
                                                                                     : YGWrapNoWrap);
        YGNodeStyleSetJustifyContent(node, toYogaJustify(style->justifyContent));
        YGNodeStyleSetAlignItems(node, toYogaAlign(style->alignItems, YGAlignStretch));
        YGNodeStyleSetAlignContent(node, toYogaAlign(style->alignContent, YGAlignStretch));
    } else {
        // Block container: children stretch across and never grow.
        YGNodeStyleSetAlignItems(node, YGAlignStretch);
        YGNodeStyleSetJustifyContent(node, YGJustifyFlexStart);
        YGNodeStyleSetFlexWrap(node, YGWrapNoWrap);
    }
    YGNodeStyleSetAlignSelf(node, toYogaAlign(style->alignSelf, YGAlignAuto));

    // A block-level child of a block container does not grow or shrink.
    const LayoutBox* parent = box.parent();
    const bool inFlexParent = parent && parent->style() && parent->style()->isFlexContainer();
    YGNodeStyleSetFlexGrow(node, inFlexParent ? style->flexGrow : 0.0f);
    YGNodeStyleSetFlexShrink(node, inFlexParent ? style->flexShrink : 0.0f);
    if (inFlexParent) {
        length(
            style->flexBasis, [&](float v) { YGNodeStyleSetFlexBasis(node, v); },
            [&](float v) { YGNodeStyleSetFlexBasisPercent(node, v); },
            [&] { YGNodeStyleSetFlexBasisAuto(node); });
    } else {
        YGNodeStyleSetFlexBasisAuto(node);
    }

    length(
        style->width, [&](float v) { YGNodeStyleSetWidth(node, v); },
        [&](float v) { YGNodeStyleSetWidthPercent(node, v); }, [&] { YGNodeStyleSetWidthAuto(node); });
    length(
        style->height, [&](float v) { YGNodeStyleSetHeight(node, v); },
        [&](float v) { YGNodeStyleSetHeightPercent(node, v); }, [&] { YGNodeStyleSetHeightAuto(node); });
    length(
        style->minWidth, [&](float v) { YGNodeStyleSetMinWidth(node, v); },
        [&](float v) { YGNodeStyleSetMinWidthPercent(node, v); }, [&] { YGNodeStyleSetMinWidth(node, YGUndefined); });
    length(
        style->minHeight, [&](float v) { YGNodeStyleSetMinHeight(node, v); },
        [&](float v) { YGNodeStyleSetMinHeightPercent(node, v); },
        [&] { YGNodeStyleSetMinHeight(node, YGUndefined); });
    length(
        style->maxWidth, [&](float v) { YGNodeStyleSetMaxWidth(node, v); },
        [&](float v) { YGNodeStyleSetMaxWidthPercent(node, v); }, [&] { YGNodeStyleSetMaxWidth(node, YGUndefined); });
    length(
        style->maxHeight, [&](float v) { YGNodeStyleSetMaxHeight(node, v); },
        [&](float v) { YGNodeStyleSetMaxHeightPercent(node, v); },
        [&] { YGNodeStyleSetMaxHeight(node, YGUndefined); });

    for (int side = 0; side < 4; ++side) {
        const YGEdge edge = kEdges[side];
        const auto index = static_cast<size_t>(side);
        length(
            style->margin[index], [&](float v) { YGNodeStyleSetMargin(node, edge, v); },
            [&](float v) { YGNodeStyleSetMarginPercent(node, edge, v); },
            [&] { YGNodeStyleSetMarginAuto(node, edge); });
        length(
            style->padding[index], [&](float v) { YGNodeStyleSetPadding(node, edge, v); },
            [&](float v) { YGNodeStyleSetPaddingPercent(node, edge, v); },
            [&] { YGNodeStyleSetPadding(node, edge, 0.0f); });
        YGNodeStyleSetBorder(node, edge, style->borderWidth[index]);
        if (style->isPositioned()) {
            length(
                style->inset[index], [&](float v) { YGNodeStyleSetPosition(node, edge, v); },
                [&](float v) { YGNodeStyleSetPositionPercent(node, edge, v); },
                [&] { YGNodeStyleSetPositionAuto(node, edge); });
        }
    }

    length(
        style->rowGap, [&](float v) { YGNodeStyleSetGap(node, YGGutterRow, v); },
        [&](float v) { YGNodeStyleSetGapPercent(node, YGGutterRow, v); },
        [&] { YGNodeStyleSetGap(node, YGGutterRow, 0.0f); });
    length(
        style->columnGap, [&](float v) { YGNodeStyleSetGap(node, YGGutterColumn, v); },
        [&](float v) { YGNodeStyleSetGapPercent(node, YGGutterColumn, v); },
        [&] { YGNodeStyleSetGap(node, YGGutterColumn, 0.0f); });

    if (box.kind() == BoxKind::Replaced) {
        YGNodeMarkDirty(node);
    }
    for (const std::unique_ptr<LayoutBox>& child : box.children()) {
        applyStyles(*child);
    }
}

void LayoutEngine::rebuildTree() {
    root_.reset();
    dom::Element* documentElement = document_.documentElement();
    if (!documentElement || !documentElement->computedStyle()) {
        return;
    }
    root_ = buildBox(*documentElement);
    treeDirty_ = false;
}

void LayoutEngine::rebuildInlineContentIfRestyled(LayoutBox& box) {
    text::InlineContent* content = box.inlineContent();
    const LayoutBox* parent = box.parent();
    if (!content || !parent || !parent->element() || !box.style()) {
        return;
    }
    if (box.styleUsedForText() == box.style()) {
        return;
    }
    if (parent->element()->isTextControl()) {
        box.setStyleUsedForText(box.style());
        content->buildLiteral(*box.style(), dom::utf16ToUtf8(parent->element()->ensureTextControl().displayText()));
        return;
    }
    // Every run holds a copy of its element's text properties, so a restyle has
    // to reshape the paragraph. The placeholders keep pointing at the same
    // atomic inline boxes; only their sizes are measured again.
    box.setStyleUsedForText(box.style());
    content->build(*parent->element(), *box.style(), content->placeholders());
}

void LayoutEngine::transferFrames(LayoutBox& box, float parentX, float parentY) {
    YGNodeRef node = box.yogaNode();
    const float x = parentX + YGNodeLayoutGetLeft(node);
    const float y = parentY + YGNodeLayoutGetTop(node);
    box.setBorderBox(Rect{x, y, YGNodeLayoutGetWidth(node), YGNodeLayoutGetHeight(node)});
    box.setEdges({YGNodeLayoutGetBorder(node, YGEdgeTop), YGNodeLayoutGetBorder(node, YGEdgeRight),
                  YGNodeLayoutGetBorder(node, YGEdgeBottom), YGNodeLayoutGetBorder(node, YGEdgeLeft)},
                 {YGNodeLayoutGetPadding(node, YGEdgeTop), YGNodeLayoutGetPadding(node, YGEdgeRight),
                  YGNodeLayoutGetPadding(node, YGEdgeBottom), YGNodeLayoutGetPadding(node, YGEdgeLeft)});

    // Re-lay the paragraph out at the width the box actually got. Yoga caches
    // measurements, so the last measure call is often a trial pass at a much
    // larger width; leaving that in place would align the text (and the
    // placeholder rects atomic inlines are positioned from) to the trial width.
    if (text::InlineContent* content = box.inlineContent(); content && !content->empty()) {
        content->layout(std::max(0.0f, box.contentBox().width));
    }

    for (const std::unique_ptr<LayoutBox>& child : box.children()) {
        transferFrames(*child, x, y);
    }
    // How far the content reaches past the padding box is what can be scrolled.
    // Measured after the children have their frames, so it sees the real extent.
    if (box.style() && box.style()->clipsOverflow()) {
        const Rect padding = box.paddingBox();
        float right = padding.width;
        float bottom = padding.height;
        for (const std::unique_ptr<LayoutBox>& child : box.children()) {
            const Rect& frame = child->borderBox();
            right = std::max(right, frame.right() - padding.x);
            bottom = std::max(bottom, frame.bottom() - padding.y);
        }
        if (const text::InlineContent* inlineContent = box.inlineContent()) {
            for (const text::InlinePlaceholder& placeholder : inlineContent->placeholders()) {
                if (placeholder.box) {
                    const Rect& frame = placeholder.box->borderBox();
                    right = std::max(right, frame.right() - padding.x);
                    bottom = std::max(bottom, frame.bottom() - padding.y);
                }
            }
        }
        box.setScrollSize(right, bottom);
    }

    // Atomic inlines sit where the paragraph placed their placeholders.
    if (const text::InlineContent* content = box.inlineContent()) {
        const Rect contentBox = box.contentBox();
        for (const text::InlinePlaceholder& placeholder : content->placeholders()) {
            if (!placeholder.box) {
                continue;
            }
            YGNodeRef inner = placeholder.box->yogaNode();
            placeholder.box->setBorderBox(Rect{contentBox.x + placeholder.x, contentBox.y + placeholder.y,
                                               YGNodeLayoutGetWidth(inner), YGNodeLayoutGetHeight(inner)});
            placeholder.box->setEdges(
                {YGNodeLayoutGetBorder(inner, YGEdgeTop), YGNodeLayoutGetBorder(inner, YGEdgeRight),
                 YGNodeLayoutGetBorder(inner, YGEdgeBottom), YGNodeLayoutGetBorder(inner, YGEdgeLeft)},
                {YGNodeLayoutGetPadding(inner, YGEdgeTop), YGNodeLayoutGetPadding(inner, YGEdgeRight),
                 YGNodeLayoutGetPadding(inner, YGEdgeBottom), YGNodeLayoutGetPadding(inner, YGEdgeLeft)});
            for (const std::unique_ptr<LayoutBox>& inlineChild : placeholder.box->children()) {
                transferFrames(*inlineChild, placeholder.box->borderBox().x, placeholder.box->borderBox().y);
            }
        }
    }
}

void LayoutEngine::layout(float viewportWidth, float viewportHeight, float devicePixelRatio) {
    viewportWidth_ = viewportWidth;
    viewportHeight_ = viewportHeight;
    devicePixelRatio_ = devicePixelRatio;
    YGConfigSetPointScaleFactor(config_, devicePixelRatio);

    // The tree is rebuilt whenever the DOM structure changed; style-only changes
    // reuse it (Stage 10 will make this incremental).
    dom::Element* documentElement = document_.documentElement();
    if (!documentElement) {
        root_.reset();
        return;
    }
    if (treeDirty_ || !root_ || root_->element() != documentElement ||
        (documentElement->dirtyBits() & dom::kDirtyLayoutTree) != 0) {
        rebuildTree();
    }
    if (!root_) {
        return;
    }

    applyStyles(*root_);
    YGNodeCalculateLayout(root_->yogaNode(), viewportWidth, viewportHeight, YGDirectionLTR);
    transferFrames(*root_, 0.0f, 0.0f);

    // Clear the layout dirty bits the tree consumed.
    for (dom::Node* node = documentElement; node; node = dom::nextInTreeOrder(node, documentElement)) {
        node->clearDirty(dom::kDirtyLayout | dom::kDirtyLayoutTree);
    }
}

LayoutBox* LayoutEngine::boxFor(const dom::Element& element) const {
    if (!root_) {
        return nullptr;
    }
    // Small trees: a linear walk is cheaper than maintaining a map.
    std::vector<LayoutBox*> stack{root_.get()};
    while (!stack.empty()) {
        LayoutBox* box = stack.back();
        stack.pop_back();
        if (box->element() == &element) {
            return box;
        }
        for (const std::unique_ptr<LayoutBox>& child : box->children()) {
            stack.push_back(child.get());
        }
        for (const std::unique_ptr<LayoutBox>& child : box->atomicInlines()) {
            stack.push_back(child.get());
        }
    }
    return nullptr;
}

LayoutBox* LayoutEngine::hitTest(float x, float y) const {
    if (!root_) {
        return nullptr;
    }
    LayoutBox* found = nullptr;
    // Depth-first, last match wins: children paint over their parents.
    std::vector<LayoutBox*> stack{root_.get()};
    while (!stack.empty()) {
        LayoutBox* box = stack.back();
        stack.pop_back();
        if (!box->borderBox().contains(x, y)) {
            continue;
        }
        if (box->element() && box->style() && box->style()->pointerEvents != css::PointerEvents::None) {
            found = box;
        }
        for (const std::unique_ptr<LayoutBox>& child : box->children()) {
            stack.push_back(child.get());
        }
        for (const std::unique_ptr<LayoutBox>& child : box->atomicInlines()) {
            stack.push_back(child.get());
        }
    }
    return found;
}

} // namespace xgu::layout
