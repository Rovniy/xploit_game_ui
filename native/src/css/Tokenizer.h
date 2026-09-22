#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace xgu::css {

enum class TokenType : uint8_t {
    Ident,
    Function,  // ident immediately followed by '(' ; value holds the name
    AtKeyword, // @media
    Hash,      // #id or #ff0000 ; value excludes '#'
    String,
    Url,      // url(...) ; value holds the unquoted target
    Number,   // value holds the text, number holds the value
    Dimension, // number + unit ; unit holds the unit
    Percentage,
    Delim, // single character such as '>' '+' '*' '/'
    Comma,
    Colon,
    Semicolon,
    LeftBrace,
    RightBrace,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    Whitespace,
    EndOfFile,
    BadString,
    BadUrl,
};

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string value; // ident/string/url/hash text, or the delim character
    std::string unit;  // Dimension only
    double number = 0.0;
    size_t offset = 0; // byte offset in the source, for error messages

    bool is(TokenType t) const { return type == t; }
    bool isDelim(char c) const { return type == TokenType::Delim && value.size() == 1 && value[0] == c; }
    bool isIdent(std::string_view name) const; // case-insensitive
};

// CSS tokenizer covering the syntax the engine's subset needs: identifiers,
// functions, hashes, strings, urls, numbers with units, and the punctuation of
// declarations and selectors. Comments and CDO/CDC are skipped.
class Tokenizer {
public:
    explicit Tokenizer(std::string_view source) : source_(source) {}

    // Next token, including Whitespace (callers usually use nextSkippingSpace).
    Token next();
    Token nextSkippingSpace();
    size_t offset() const { return position_; }
    bool atEnd() const { return position_ >= source_.size(); }

private:
    char peek(size_t lookahead = 0) const;
    bool startsIdent(size_t at) const;
    std::string consumeIdent();
    Token consumeNumeric();
    Token consumeString(char quote);
    Token consumeUrl();
    void consumeComment();

    std::string_view source_;
    size_t position_ = 0;
};

} // namespace xgu::css
