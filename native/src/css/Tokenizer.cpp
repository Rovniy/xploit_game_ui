#include "css/Tokenizer.h"

#include <cctype>
#include <cstdlib>

namespace xgu::css {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isNameStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}
bool isNameChar(char c) { return isNameStart(c) || isDigit(c) || c == '-'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

} // namespace

bool Token::isIdent(std::string_view name) const {
    if (type != TokenType::Ident || value.size() != name.size()) {
        return false;
    }
    for (size_t i = 0; i < name.size(); ++i) {
        if (lower(value[i]) != lower(name[i])) {
            return false;
        }
    }
    return true;
}

char Tokenizer::peek(size_t lookahead) const {
    const size_t index = position_ + lookahead;
    return index < source_.size() ? source_[index] : '\0';
}

bool Tokenizer::startsIdent(size_t at) const {
    const char c = at < source_.size() ? source_[at] : '\0';
    if (isNameStart(c)) {
        return true;
    }
    if (c == '-') {
        const char next = at + 1 < source_.size() ? source_[at + 1] : '\0';
        return isNameStart(next) || next == '-';
    }
    return false;
}

std::string Tokenizer::consumeIdent() {
    const size_t start = position_;
    if (peek() == '-') {
        ++position_;
    }
    while (position_ < source_.size() && isNameChar(source_[position_])) {
        ++position_;
    }
    return std::string(source_.substr(start, position_ - start));
}

void Tokenizer::consumeComment() {
    position_ += 2; // "/*"
    while (position_ < source_.size()) {
        if (source_[position_] == '*' && position_ + 1 < source_.size() && source_[position_ + 1] == '/') {
            position_ += 2;
            return;
        }
        ++position_;
    }
}

Token Tokenizer::consumeNumeric() {
    const size_t start = position_;
    if (peek() == '+' || peek() == '-') {
        ++position_;
    }
    while (isDigit(peek())) {
        ++position_;
    }
    if (peek() == '.' && isDigit(peek(1))) {
        ++position_;
        while (isDigit(peek())) {
            ++position_;
        }
    }
    if ((peek() == 'e' || peek() == 'E') &&
        (isDigit(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && isDigit(peek(2))))) {
        position_ += 2;
        while (isDigit(peek())) {
            ++position_;
        }
    }
    const std::string text(source_.substr(start, position_ - start));

    Token token;
    token.offset = start;
    token.number = std::strtod(text.c_str(), nullptr);
    token.value = text;
    if (peek() == '%') {
        ++position_;
        token.type = TokenType::Percentage;
        return token;
    }
    if (startsIdent(position_)) {
        token.type = TokenType::Dimension;
        token.unit = consumeIdent();
        return token;
    }
    token.type = TokenType::Number;
    return token;
}

Token Tokenizer::consumeString(char quote) {
    Token token;
    token.type = TokenType::String;
    token.offset = position_;
    ++position_; // opening quote
    std::string text;
    while (position_ < source_.size()) {
        const char c = source_[position_];
        if (c == quote) {
            ++position_;
            token.value = std::move(text);
            return token;
        }
        if (c == '\n') {
            token.type = TokenType::BadString;
            token.value = std::move(text);
            return token;
        }
        if (c == '\\' && position_ + 1 < source_.size()) {
            ++position_;
            text.push_back(source_[position_]);
            ++position_;
            continue;
        }
        text.push_back(c);
        ++position_;
    }
    token.type = TokenType::BadString;
    token.value = std::move(text);
    return token;
}

Token Tokenizer::consumeUrl() {
    // Called just after "url(".
    Token token;
    token.type = TokenType::Url;
    token.offset = position_;
    while (isSpace(peek())) {
        ++position_;
    }
    if (peek() == '"' || peek() == '\'') {
        Token string = consumeString(peek());
        while (isSpace(peek())) {
            ++position_;
        }
        if (peek() == ')') {
            ++position_;
        }
        token.type = string.type == TokenType::BadString ? TokenType::BadUrl : TokenType::Url;
        token.value = std::move(string.value);
        return token;
    }
    std::string text;
    while (position_ < source_.size()) {
        const char c = source_[position_];
        if (c == ')') {
            ++position_;
            token.value = std::move(text);
            return token;
        }
        if (isSpace(c)) {
            while (isSpace(peek())) {
                ++position_;
            }
            if (peek() == ')') {
                ++position_;
                token.value = std::move(text);
                return token;
            }
            token.type = TokenType::BadUrl;
            return token;
        }
        text.push_back(c);
        ++position_;
    }
    token.type = TokenType::BadUrl;
    token.value = std::move(text);
    return token;
}

Token Tokenizer::next() {
    if (position_ >= source_.size()) {
        Token token;
        token.type = TokenType::EndOfFile;
        token.offset = position_;
        return token;
    }

    const size_t start = position_;
    const char c = source_[position_];

    if (isSpace(c)) {
        while (position_ < source_.size() && isSpace(source_[position_])) {
            ++position_;
        }
        Token token;
        token.type = TokenType::Whitespace;
        token.offset = start;
        return token;
    }
    if (c == '/' && peek(1) == '*') {
        consumeComment();
        return next();
    }
    if (c == '"' || c == '\'') {
        return consumeString(c);
    }
    if (c == '#') {
        ++position_;
        Token token;
        token.type = TokenType::Hash;
        token.offset = start;
        while (position_ < source_.size() && isNameChar(source_[position_])) {
            token.value.push_back(source_[position_]);
            ++position_;
        }
        return token;
    }
    if (isDigit(c) || ((c == '+' || c == '-' || c == '.') && (isDigit(peek(1)) || (peek(1) == '.' && isDigit(peek(2)))))) {
        return consumeNumeric();
    }
    if (c == '@' && startsIdent(position_ + 1)) {
        ++position_;
        Token token;
        token.type = TokenType::AtKeyword;
        token.offset = start;
        token.value = consumeIdent();
        return token;
    }
    if (startsIdent(position_)) {
        Token token;
        token.offset = start;
        token.value = consumeIdent();
        if (peek() == '(') {
            ++position_;
            std::string lowered = token.value;
            for (char& ch : lowered) {
                ch = lower(ch);
            }
            if (lowered == "url") {
                Token url = consumeUrl();
                url.offset = start;
                return url;
            }
            token.type = TokenType::Function;
            return token;
        }
        token.type = TokenType::Ident;
        return token;
    }

    ++position_;
    Token token;
    token.offset = start;
    switch (c) {
    case ',':
        token.type = TokenType::Comma;
        break;
    case ':':
        token.type = TokenType::Colon;
        break;
    case ';':
        token.type = TokenType::Semicolon;
        break;
    case '{':
        token.type = TokenType::LeftBrace;
        break;
    case '}':
        token.type = TokenType::RightBrace;
        break;
    case '(':
        token.type = TokenType::LeftParen;
        break;
    case ')':
        token.type = TokenType::RightParen;
        break;
    case '[':
        token.type = TokenType::LeftBracket;
        break;
    case ']':
        token.type = TokenType::RightBracket;
        break;
    default:
        token.type = TokenType::Delim;
        token.value.push_back(c);
        break;
    }
    return token;
}

Token Tokenizer::nextSkippingSpace() {
    Token token = next();
    while (token.type == TokenType::Whitespace) {
        token = next();
    }
    return token;
}

} // namespace xgu::css
