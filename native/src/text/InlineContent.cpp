#include "text/InlineContent.h"

#include "core/Log.h"
#include "dom/Element.h"
#include "text/FontManager.h"

#include <include/core/SkCanvas.h>
#include <include/core/SkFontStyle.h>
#include <include/core/SkPaint.h>
#include <modules/skparagraph/include/ParagraphBuilder.h>
#include <modules/skparagraph/include/ParagraphStyle.h>
#include <modules/skparagraph/include/TextStyle.h>

#include <algorithm>
#include <cctype>

namespace xgu::text {
namespace {

using namespace skia::textlayout;

bool isHtmlSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

SkColor toSkColor(const css::Color& color) { return static_cast<SkColor>(color.toArgb()); }

SkFontStyle toSkFontStyle(const css::StyleValues& style) {
    const SkFontStyle::Slant slant = style.fontStyle == css::FontStyle::Normal ? SkFontStyle::kUpright_Slant
                                                                              : SkFontStyle::kItalic_Slant;
    return SkFontStyle(style.fontWeight, SkFontStyle::kNormal_Width, slant);
}

TextStyle toTextStyle(const css::StyleValues& style) {
    TextStyle textStyle;
    textStyle.setColor(toSkColor(style.color));
    textStyle.setFontSize(style.fontSize);
    textStyle.setFontStyle(toSkFontStyle(style));
    textStyle.setLetterSpacing(style.letterSpacing);

    std::vector<SkString> families;
    for (const std::string& family : style.fontFamily) {
        families.emplace_back(family.c_str());
    }
    if (families.empty()) {
        families.emplace_back(FontManager::instance().defaultFamily().c_str());
    }
    textStyle.setFontFamilies(families);

    if (style.lineHeight > 0.0f && style.fontSize > 0.0f) {
        textStyle.setHeight(style.lineHeight / style.fontSize);
        textStyle.setHeightOverride(true);
    }
    if (style.textDecorationLine != css::kDecorationNone) {
        int decoration = TextDecoration::kNoDecoration;
        if (style.textDecorationLine & css::kDecorationUnderline) decoration |= TextDecoration::kUnderline;
        if (style.textDecorationLine & css::kDecorationOverline) decoration |= TextDecoration::kOverline;
        if (style.textDecorationLine & css::kDecorationLineThrough) decoration |= TextDecoration::kLineThrough;
        textStyle.setDecoration(static_cast<TextDecoration>(decoration));
        textStyle.setDecorationColor(toSkColor(style.textDecorationColor));
    }
    return textStyle;
}

TextAlign toTextAlign(css::TextAlign align) {
    switch (align) {
    case css::TextAlign::Left:
    case css::TextAlign::Start:
        return TextAlign::kLeft;
    case css::TextAlign::Right:
        return TextAlign::kRight;
    case css::TextAlign::Center:
        return TextAlign::kCenter;
    case css::TextAlign::Justify:
        return TextAlign::kJustify;
    }
    return TextAlign::kLeft;
}

// Collects text runs from the inline descendants of `container`.
void collectRuns(const dom::Node& node, const css::StyleValues& inheritedStyle, std::string& text,
                 std::vector<std::pair<size_t, css::StyleValues>>& runStarts, bool& lastWasSpace) {
    for (size_t i = 0; i < node.childCount(); ++i) {
        const dom::Node* child = node.childAt(i);
        if (child->isComment()) {
            continue;
        }
        if (child->isText()) {
            const auto& data = static_cast<const dom::CharacterData*>(child)->data();
            std::string piece = applyTextTransform(data, inheritedStyle.textTransform);
            piece = processWhiteSpace(piece, inheritedStyle.whiteSpace, lastWasSpace, false);
            if (piece.empty()) {
                continue;
            }
            lastWasSpace = isHtmlSpace(piece.back());
            runStarts.emplace_back(text.size(), inheritedStyle);
            text += piece;
            continue;
        }
        if (!child->isElement()) {
            continue;
        }
        const auto& element = static_cast<const dom::Element&>(*child);
        const css::ComputedStyle* style = element.computedStyle();
        if (!style || style->display == css::Display::None) {
            continue;
        }
        // Atomic inlines are represented by placeholders added by the caller.
        if (style->display != css::Display::Inline) {
            continue;
        }
        collectRuns(element, *style, text, runStarts, lastWasSpace);
    }
}

} // namespace

std::string applyTextTransform(std::string_view text, css::TextTransform transform) {
    if (transform == css::TextTransform::None) {
        return std::string(text);
    }
    std::string result(text);
    // ASCII-only transform; full Unicode casing is out of scope for the MVP.
    bool atWordStart = true;
    for (char& c : result) {
        const unsigned char u = static_cast<unsigned char>(c);
        switch (transform) {
        case css::TextTransform::Uppercase:
            c = static_cast<char>(std::toupper(u));
            break;
        case css::TextTransform::Lowercase:
            c = static_cast<char>(std::tolower(u));
            break;
        case css::TextTransform::Capitalize:
            if (atWordStart) {
                c = static_cast<char>(std::toupper(u));
            }
            atWordStart = isHtmlSpace(c);
            break;
        case css::TextTransform::None:
            break;
        }
    }
    return result;
}

std::string processWhiteSpace(std::string_view text, css::WhiteSpace mode, bool trimLeading, bool trimTrailing) {
    const bool collapse = mode == css::WhiteSpace::Normal || mode == css::WhiteSpace::NoWrap ||
                          mode == css::WhiteSpace::PreLine;
    const bool keepNewlines = mode == css::WhiteSpace::Pre || mode == css::WhiteSpace::PreWrap ||
                              mode == css::WhiteSpace::PreLine;
    if (!collapse) {
        return std::string(text);
    }

    std::string result;
    result.reserve(text.size());
    bool pendingSpace = trimLeading;
    for (char c : text) {
        if (isHtmlSpace(c)) {
            if (keepNewlines && c == '\n') {
                while (!result.empty() && result.back() == ' ') {
                    result.pop_back();
                }
                result.push_back('\n');
                pendingSpace = true;
                continue;
            }
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !result.empty()) {
            result.push_back(' ');
        } else if (pendingSpace && !trimLeading) {
            result.push_back(' ');
        }
        pendingSpace = false;
        result.push_back(c);
    }
    if (pendingSpace && !trimTrailing && !result.empty()) {
        result.push_back(' ');
    }
    return result;
}

InlineContent::InlineContent() = default;
InlineContent::~InlineContent() = default;

void InlineContent::build(const dom::Element& container, const css::ComputedStyle& containerStyle,
                          std::vector<InlinePlaceholder> placeholders) {
    text_.clear();
    runs_.clear();
    placeholders_ = std::move(placeholders);
    containerStyle_ = containerStyle;

    std::vector<std::pair<size_t, css::StyleValues>> runStarts;
    bool lastWasSpace = true; // leading white space of a block is dropped
    collectRuns(container, containerStyle, text_, runStarts, lastWasSpace);

    // Trailing collapsible space at the end of a block is dropped too.
    if (containerStyle.whiteSpace == css::WhiteSpace::Normal ||
        containerStyle.whiteSpace == css::WhiteSpace::NoWrap) {
        while (!text_.empty() && text_.back() == ' ') {
            text_.pop_back();
        }
    }

    for (size_t i = 0; i < runStarts.size(); ++i) {
        const size_t start = runStarts[i].first;
        if (start >= text_.size()) {
            break;
        }
        const size_t end = (i + 1 < runStarts.size()) ? std::min(runStarts[i + 1].first, text_.size()) : text_.size();
        if (end > start) {
            runs_.push_back(Run{start, end - start, runStarts[i].second});
        }
    }
    invalidate();
}

void InlineContent::setPlaceholderSizes(const std::vector<InlinePlaceholder>& sizes) {
    bool changed = sizes.size() != placeholders_.size();
    for (size_t i = 0; i < sizes.size() && i < placeholders_.size(); ++i) {
        if (placeholders_[i].width != sizes[i].width || placeholders_[i].height != sizes[i].height ||
            placeholders_[i].box != sizes[i].box) {
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    placeholders_ = sizes;
    invalidate();
}

void InlineContent::invalidate() {
    paragraph_.reset();
    laidOutWidth_ = -1.0f;
    dirty_ = true;
}

void InlineContent::ensureParagraph() {
    if (paragraph_ && !dirty_) {
        return;
    }
    sk_sp<FontCollection> fonts = FontManager::instance().fontCollection();
    sk_sp<SkUnicode> unicode = FontManager::instance().unicode();
    if (!fonts || !unicode) {
        return;
    }

    ParagraphStyle paragraphStyle;
    paragraphStyle.setTextAlign(toTextAlign(containerStyle_.textAlign));
    paragraphStyle.setTextStyle(toTextStyle(containerStyle_));
    if (containerStyle_.whiteSpace == css::WhiteSpace::NoWrap ||
        containerStyle_.whiteSpace == css::WhiteSpace::Pre) {
        paragraphStyle.setMaxLines(1);
    }
    if (containerStyle_.textOverflow == css::TextOverflow::Ellipsis) {
        paragraphStyle.setEllipsis(SkString("\xE2\x80\xA6"));
    }

    std::unique_ptr<ParagraphBuilder> builder = ParagraphBuilder::make(paragraphStyle, fonts, unicode);
    if (!builder) {
        XGU_LOG_ERROR("text: ParagraphBuilder::make failed");
        return;
    }

    size_t placeholderIndex = 0;
    if (runs_.empty() && !text_.empty()) {
        builder->pushStyle(toTextStyle(containerStyle_));
        builder->addText(text_.c_str(), text_.size());
        builder->pop();
    }
    for (const Run& run : runs_) {
        builder->pushStyle(toTextStyle(run.style));
        builder->addText(text_.c_str() + run.start, run.length);
        builder->pop();
    }
    for (; placeholderIndex < placeholders_.size(); ++placeholderIndex) {
        const InlinePlaceholder& placeholder = placeholders_[placeholderIndex];
        PlaceholderStyle style(placeholder.width, placeholder.height, PlaceholderAlignment::kBaseline,
                               TextBaseline::kAlphabetic, 0.0f);
        builder->addPlaceholder(style);
    }

    paragraph_ = builder->Build();
    dirty_ = false;
    laidOutWidth_ = -1.0f;
}

InlineContent::Size InlineContent::layout(float width) {
    ensureParagraph();
    if (!paragraph_) {
        return Size{0.0f, 0.0f};
    }
    if (laidOutWidth_ == width) {
        return lastSize_;
    }
    paragraph_->layout(width);
    laidOutWidth_ = width;
    lastSize_ = Size{std::min(width, std::ceil(paragraph_->getLongestLine())), paragraph_->getHeight()};

    // Record where the placeholders ended up so the boxes can be positioned.
    const std::vector<TextBox> boxes = paragraph_->getRectsForPlaceholders();
    for (size_t i = 0; i < placeholders_.size() && i < boxes.size(); ++i) {
        placeholders_[i].x = boxes[i].rect.left();
        placeholders_[i].y = boxes[i].rect.top();
    }
    return lastSize_;
}

float InlineContent::maxIntrinsicWidth() {
    ensureParagraph();
    if (!paragraph_) {
        return 0.0f;
    }
    if (laidOutWidth_ < 0.0f) {
        paragraph_->layout(1.0e7f);
        laidOutWidth_ = 1.0e7f;
    }
    return paragraph_->getMaxIntrinsicWidth();
}

float InlineContent::minIntrinsicWidth() {
    ensureParagraph();
    if (!paragraph_) {
        return 0.0f;
    }
    if (laidOutWidth_ < 0.0f) {
        paragraph_->layout(1.0e7f);
        laidOutWidth_ = 1.0e7f;
    }
    return paragraph_->getMinIntrinsicWidth();
}

float InlineContent::firstBaseline() {
    ensureParagraph();
    if (!paragraph_) {
        return 0.0f;
    }
    return paragraph_->getAlphabeticBaseline();
}

void InlineContent::paint(SkCanvas* canvas, float x, float y) {
    if (!paragraph_ || !canvas) {
        return;
    }
    paragraph_->paint(canvas, x, y);
}

} // namespace xgu::text
