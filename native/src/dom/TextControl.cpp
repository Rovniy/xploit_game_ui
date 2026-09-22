#include "dom/TextControl.h"

#include "dom/Element.h"

#include <algorithm>

namespace xgu::dom {
namespace {

const Atom& typeAttribute() {
    static const Atom atom("type");
    return atom;
}

const Atom& valueAttribute() {
    static const Atom atom("value");
    return atom;
}

const Atom& placeholderAttribute() {
    static const Atom atom("placeholder");
    return atom;
}

bool isHighSurrogate(char16_t unit) { return unit >= 0xD800 && unit <= 0xDBFF; }
bool isLowSurrogate(char16_t unit) { return unit >= 0xDC00 && unit <= 0xDFFF; }

} // namespace

std::u16string utf8ToUtf16(std::string_view text) {
    std::u16string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const auto byte = static_cast<unsigned char>(text[i]);
        char32_t code = 0;
        size_t extra = 0;
        if (byte < 0x80) {
            code = byte;
        } else if ((byte & 0xE0) == 0xC0) {
            code = byte & 0x1Fu;
            extra = 1;
        } else if ((byte & 0xF0) == 0xE0) {
            code = byte & 0x0Fu;
            extra = 2;
        } else if ((byte & 0xF8) == 0xF0) {
            code = byte & 0x07u;
            extra = 3;
        } else {
            ++i; // stray continuation byte
            continue;
        }
        if (i + extra >= text.size() + (extra == 0 ? 1 : 0) && extra > 0 && i + extra >= text.size()) {
            break; // truncated sequence
        }
        for (size_t k = 1; k <= extra; ++k) {
            const auto continuation = static_cast<unsigned char>(text[i + k]);
            if ((continuation & 0xC0) != 0x80) {
                code = 0xFFFD;
                extra = k - 1;
                break;
            }
            code = (code << 6) | (continuation & 0x3Fu);
        }
        i += extra + 1;
        if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            code = 0xFFFD;
        }
        if (code >= 0x10000) {
            code -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 + (code >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
        } else {
            result.push_back(static_cast<char16_t>(code));
        }
    }
    return result;
}

std::string utf16ToUtf8(std::u16string_view text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        char32_t code = text[i];
        if (isHighSurrogate(text[i]) && i + 1 < text.size() && isLowSurrogate(text[i + 1])) {
            code = 0x10000 + ((static_cast<char32_t>(text[i]) - 0xD800) << 10) +
                   (static_cast<char32_t>(text[i + 1]) - 0xDC00);
            ++i;
        } else if (isHighSurrogate(text[i]) || isLowSurrogate(text[i])) {
            code = 0xFFFD; // unpaired surrogate
        }
        if (code < 0x80) {
            result.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (code >> 6)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (code >> 12)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (code >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    return result;
}

TextControl::TextControl(Element& owner) : owner_(owner) {
    text_ = utf8ToUtf16(owner.getAttributeOrEmpty(valueAttribute()));
    committed_ = text_;
    caret_ = text_.size();
    anchor_ = caret_;
}

bool TextControl::isMultiline() const { return owner_.knownTag() == html::HtmlTag::Textarea; }

bool TextControl::isPassword() const {
    if (owner_.knownTag() != html::HtmlTag::Input) {
        return false;
    }
    const std::string* type = owner_.getAttribute(typeAttribute());
    return type && Atom(*type).equalsIgnoringCase("password");
}

bool TextControl::isEditable() const {
    if (owner_.knownTag() == html::HtmlTag::Textarea) {
        return true;
    }
    if (owner_.knownTag() != html::HtmlTag::Input) {
        return false;
    }
    const std::string* type = owner_.getAttribute(typeAttribute());
    if (!type) {
        return true; // text is the default
    }
    const Atom value(*type);
    return value.equalsIgnoringCase("text") || value.equalsIgnoringCase("password") ||
           value.equalsIgnoringCase("search") || value.equalsIgnoringCase("email") ||
           value.equalsIgnoringCase("url") || value.equalsIgnoringCase("tel") ||
           value.equalsIgnoringCase("number");
}

std::string TextControl::value() const { return utf16ToUtf8(text_); }

bool TextControl::setValue(std::string_view utf8) {
    std::u16string updated = utf8ToUtf16(utf8);
    if (updated == text_) {
        return false;
    }
    text_ = std::move(updated);
    caret_ = text_.size();
    anchor_ = caret_;
    return true;
}

bool TextControl::showingPlaceholder() const {
    return text_.empty() && !owner_.getAttributeOrEmpty(placeholderAttribute()).empty();
}

std::u16string TextControl::displayText() const {
    if (text_.empty()) {
        return utf8ToUtf16(owner_.getAttributeOrEmpty(placeholderAttribute()));
    }
    if (!isPassword()) {
        return text_;
    }
    // One bullet per code point, so the caret offsets still line up.
    std::u16string masked;
    masked.reserve(text_.size());
    for (size_t i = 0; i < text_.size(); ++i) {
        masked.push_back(u'•');
        if (isHighSurrogate(text_[i]) && i + 1 < text_.size() && isLowSurrogate(text_[i + 1])) {
            masked.push_back(u'•'); // keep the offset mapping one-to-one
            ++i;
        }
    }
    return masked;
}

size_t TextControl::clamp(size_t offset) const {
    offset = std::min(offset, text_.size());
    // Never land between the halves of a surrogate pair.
    if (offset > 0 && offset < text_.size() && isLowSurrogate(text_[offset]) && isHighSurrogate(text_[offset - 1])) {
        --offset;
    }
    return offset;
}

void TextControl::setSelection(size_t start, size_t end) {
    anchor_ = clamp(start);
    caret_ = clamp(end);
}

void TextControl::collapseTo(size_t offset) {
    caret_ = clamp(offset);
    anchor_ = caret_;
}

void TextControl::selectAll() {
    anchor_ = 0;
    caret_ = text_.size();
}

bool TextControl::deleteSelection() {
    if (!hasSelection()) {
        return false;
    }
    const size_t start = selectionStart();
    const size_t end = selectionEnd();
    text_.erase(start, end - start);
    collapseTo(start);
    return true;
}

bool TextControl::insertText(std::u16string_view text) {
    if (text.empty()) {
        return deleteSelection();
    }
    std::u16string insertion(text);
    if (!isMultiline()) {
        // A single-line field never holds newlines; a pasted one becomes a space.
        std::replace(insertion.begin(), insertion.end(), u'\n', u' ');
        std::replace(insertion.begin(), insertion.end(), u'\r', u' ');
    }
    deleteSelection();
    text_.insert(caret_, insertion);
    collapseTo(caret_ + insertion.size());
    return true;
}

bool TextControl::deleteBackward() {
    if (deleteSelection()) {
        return true;
    }
    if (caret_ == 0) {
        return false;
    }
    size_t start = caret_ - 1;
    if (start > 0 && isLowSurrogate(text_[start]) && isHighSurrogate(text_[start - 1])) {
        --start;
    }
    text_.erase(start, caret_ - start);
    collapseTo(start);
    return true;
}

bool TextControl::deleteForward() {
    if (deleteSelection()) {
        return true;
    }
    if (caret_ >= text_.size()) {
        return false;
    }
    size_t end = caret_ + 1;
    if (end < text_.size() && isHighSurrogate(text_[caret_]) && isLowSurrogate(text_[end])) {
        ++end;
    }
    text_.erase(caret_, end - caret_);
    collapseTo(caret_);
    return true;
}

void TextControl::moveLeft(bool extend) {
    size_t target = caret_;
    if (!extend && hasSelection()) {
        target = selectionStart();
    } else if (target > 0) {
        --target;
        if (target > 0 && isLowSurrogate(text_[target]) && isHighSurrogate(text_[target - 1])) {
            --target;
        }
    }
    caret_ = clamp(target);
    if (!extend) {
        anchor_ = caret_;
    }
}

void TextControl::moveRight(bool extend) {
    size_t target = caret_;
    if (!extend && hasSelection()) {
        target = selectionEnd();
    } else if (target < text_.size()) {
        ++target;
        if (target < text_.size() && isLowSurrogate(text_[target]) && isHighSurrogate(text_[target - 1])) {
            ++target;
        }
    }
    caret_ = clamp(target);
    if (!extend) {
        anchor_ = caret_;
    }
}

void TextControl::moveToStart(bool extend) {
    caret_ = 0;
    if (!extend) {
        anchor_ = caret_;
    }
}

void TextControl::moveToEnd(bool extend) {
    caret_ = text_.size();
    if (!extend) {
        anchor_ = caret_;
    }
}

size_t TextControl::utf16ToUtf8Offset(size_t offset) const {
    return utf16ToUtf8(std::u16string_view(text_).substr(0, std::min(offset, text_.size()))).size();
}

size_t TextControl::utf8ToUtf16Offset(size_t offset) const {
    const std::string utf8 = value();
    return utf8ToUtf16(std::string_view(utf8).substr(0, std::min(offset, utf8.size()))).size();
}

} // namespace xgu::dom
