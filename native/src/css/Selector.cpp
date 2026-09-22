#include "css/Selector.h"

#include <algorithm>
#include <cctype>

namespace xgu::css {
namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' ||
           static_cast<unsigned char>(c) >= 0x80;
}

bool isIdentChar(char c) { return isIdentStart(c) || (c >= '0' && c <= '9'); }

PseudoClass pseudoFromName(std::string_view name) {
    if (name == "hover") return PseudoClass::Hover;
    if (name == "active") return PseudoClass::Active;
    if (name == "focus") return PseudoClass::Focus;
    if (name == "focus-within") return PseudoClass::FocusWithin;
    if (name == "disabled") return PseudoClass::Disabled;
    if (name == "enabled") return PseudoClass::Enabled;
    if (name == "checked") return PseudoClass::Checked;
    if (name == "first-child") return PseudoClass::FirstChild;
    if (name == "last-child") return PseudoClass::LastChild;
    if (name == "only-child") return PseudoClass::OnlyChild;
    if (name == "empty") return PseudoClass::Empty;
    if (name == "root") return PseudoClass::Root;
    if (name == "not") return PseudoClass::Not;
    return PseudoClass::Unknown;
}

class Parser {
public:
    Parser(std::string_view text, std::string* error) : text_(text), error_(error) {}

    std::optional<SelectorList> parseList(char terminator = '\0') {
        SelectorList list;
        skipSpace();
        if (atEnd()) {
            return fail("empty selector");
        }
        for (;;) {
            std::optional<Selector> selector = parseComplex(terminator);
            if (!selector) {
                return std::nullopt;
            }
            selector->specificity = computeSpecificity(*selector);
            list.selectors.push_back(std::move(*selector));
            skipSpace();
            if (atEnd() || peek() == terminator) {
                break;
            }
            if (peek() != ',') {
                return fail("expected ',' between selectors");
            }
            ++position_;
            skipSpace();
        }
        return list;
    }

    size_t position() const { return position_; }

private:
    std::optional<Selector> parseComplex(char terminator) {
        Selector selector;
        Combinator pending = Combinator::None;
        bool expectCompound = false;
        for (;;) {
            skipSpace();
            if (atEnd() || peek() == ',' || peek() == terminator) {
                if (expectCompound) {
                    return fail("expected a compound selector after the combinator");
                }
                break;
            }
            std::optional<CompoundSelector> compound = parseCompound(terminator);
            if (!compound) {
                return std::nullopt;
            }
            compound->combinatorToLeft = pending;
            selector.compounds.push_back(std::move(*compound));
            expectCompound = false;

            // Determine the combinator to the next compound.
            const size_t before = position_;
            bool sawSpace = false;
            while (!atEnd() && isSpace(peek())) {
                sawSpace = true;
                ++position_;
            }
            if (atEnd() || peek() == ',' || peek() == terminator) {
                position_ = before;
                break;
            }
            if (peek() == '>') {
                pending = Combinator::Child;
                expectCompound = true;
                ++position_;
            } else if (peek() == '+') {
                pending = Combinator::NextSibling;
                expectCompound = true;
                ++position_;
            } else if (peek() == '~') {
                pending = Combinator::SubsequentSibling;
                expectCompound = true;
                ++position_;
            } else if (sawSpace) {
                pending = Combinator::Descendant;
            } else {
                return fail("expected a combinator");
            }
        }
        if (selector.compounds.empty()) {
            return fail("empty selector");
        }
        return selector;
    }

    std::optional<CompoundSelector> parseCompound(char terminator) {
        CompoundSelector compound;
        bool any = false;
        for (;;) {
            if (atEnd()) {
                break;
            }
            const char c = peek();
            if (c == '*') {
                ++position_;
                compound.universal = true;
                any = true;
            } else if (c == '#') {
                ++position_;
                const std::string name = parseIdent();
                if (name.empty()) {
                    return fail("expected an id after '#'");
                }
                compound.id = Atom(name);
                any = true;
            } else if (c == '.') {
                ++position_;
                const std::string name = parseIdent();
                if (name.empty()) {
                    return fail("expected a class name after '.'");
                }
                compound.classes.push_back(Atom(name));
                any = true;
            } else if (c == '[') {
                std::optional<AttributeSelector> attribute = parseAttribute();
                if (!attribute) {
                    return std::nullopt;
                }
                compound.attributes.push_back(std::move(*attribute));
                any = true;
            } else if (c == ':') {
                std::optional<PseudoSelector> pseudo = parsePseudo();
                if (!pseudo) {
                    return std::nullopt;
                }
                compound.pseudos.push_back(std::move(*pseudo));
                any = true;
            } else if (isIdentStart(c)) {
                if (!compound.tagName.empty() || compound.universal) {
                    break; // a second type selector starts a new compound: invalid
                }
                compound.tagName = Atom::lowered(parseIdent());
                any = true;
            } else {
                break;
            }
            if (!atEnd() && (isSpace(peek()) || peek() == ',' || peek() == '>' || peek() == '+' || peek() == '~' ||
                             peek() == terminator)) {
                break;
            }
        }
        if (!any) {
            return fail("expected a selector");
        }
        return compound;
    }

    std::optional<AttributeSelector> parseAttribute() {
        ++position_; // '['
        skipSpace();
        AttributeSelector attribute;
        const std::string name = parseIdent();
        if (name.empty()) {
            return fail("expected an attribute name");
        }
        attribute.name = Atom::lowered(name);
        skipSpace();
        if (atEnd()) {
            return fail("unterminated attribute selector");
        }
        if (peek() == ']') {
            ++position_;
            attribute.match = AttributeMatch::Present;
            return attribute;
        }
        switch (peek()) {
        case '=':
            attribute.match = AttributeMatch::Exact;
            ++position_;
            break;
        case '~':
            attribute.match = AttributeMatch::Includes;
            position_ += 2;
            break;
        case '|':
            attribute.match = AttributeMatch::DashMatch;
            position_ += 2;
            break;
        case '^':
            attribute.match = AttributeMatch::Prefix;
            position_ += 2;
            break;
        case '$':
            attribute.match = AttributeMatch::Suffix;
            position_ += 2;
            break;
        case '*':
            attribute.match = AttributeMatch::Substring;
            position_ += 2;
            break;
        default:
            return fail("unsupported attribute operator");
        }
        if (position_ > text_.size()) {
            return fail("unterminated attribute selector");
        }
        skipSpace();
        std::optional<std::string> value = parseAttributeValue();
        if (!value) {
            return std::nullopt;
        }
        attribute.value = std::move(*value);
        skipSpace();
        if (!atEnd() && (peek() == 'i' || peek() == 'I')) {
            attribute.caseInsensitive = true;
            ++position_;
            skipSpace();
        }
        if (atEnd() || peek() != ']') {
            return fail("expected ']'");
        }
        ++position_;
        return attribute;
    }

    std::optional<std::string> parseAttributeValue() {
        if (atEnd()) {
            return fail("expected an attribute value");
        }
        const char quote = peek();
        if (quote == '"' || quote == '\'') {
            ++position_;
            std::string value;
            while (!atEnd() && peek() != quote) {
                value.push_back(text_[position_++]);
            }
            if (atEnd()) {
                return fail("unterminated string");
            }
            ++position_;
            return value;
        }
        const std::string value = parseIdent();
        if (value.empty()) {
            return fail("expected an attribute value");
        }
        return value;
    }

    std::optional<PseudoSelector> parsePseudo() {
        ++position_; // ':'
        if (!atEnd() && peek() == ':') {
            return fail("pseudo-elements are not supported");
        }
        const std::string name = parseIdent();
        if (name.empty()) {
            return fail("expected a pseudo-class name");
        }
        PseudoSelector pseudo;
        std::string lowered = name;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        pseudo.kind = pseudoFromName(lowered);
        if (pseudo.kind == PseudoClass::Unknown) {
            return fail("unsupported pseudo-class ':" + lowered + "'");
        }
        if (!atEnd() && peek() == '(') {
            if (pseudo.kind != PseudoClass::Not) {
                return fail("pseudo-class ':" + lowered + "' takes no argument");
            }
            ++position_;
            Parser inner(text_.substr(position_), error_);
            std::optional<SelectorList> nested = inner.parseList(')');
            if (!nested) {
                return std::nullopt;
            }
            position_ += inner.position();
            if (atEnd() || peek() != ')') {
                return fail("expected ')'");
            }
            ++position_;
            pseudo.arguments = std::move(nested->selectors);
        } else if (pseudo.kind == PseudoClass::Not) {
            return fail("':not' requires an argument");
        }
        return pseudo;
    }

    std::string parseIdent() {
        if (atEnd() || !isIdentStart(peek())) {
            return {};
        }
        const size_t start = position_;
        while (!atEnd() && isIdentChar(peek())) {
            ++position_;
        }
        return std::string(text_.substr(start, position_ - start));
    }

    void skipSpace() {
        while (!atEnd() && isSpace(peek())) {
            ++position_;
        }
    }

    bool atEnd() const { return position_ >= text_.size(); }
    char peek() const { return text_[position_]; }

    std::nullopt_t fail(const std::string& message) {
        if (error_ && error_->empty()) {
            *error_ = message;
        }
        return std::nullopt;
    }

    std::string_view text_;
    std::string* error_;
    size_t position_ = 0;
};

} // namespace

uint32_t computeSpecificity(const Selector& selector) {
    uint32_t ids = 0;
    uint32_t classes = 0;
    uint32_t types = 0;
    for (const CompoundSelector& compound : selector.compounds) {
        if (!compound.id.empty()) {
            ++ids;
        }
        classes += static_cast<uint32_t>(compound.classes.size());
        classes += static_cast<uint32_t>(compound.attributes.size());
        for (const PseudoSelector& pseudo : compound.pseudos) {
            if (pseudo.kind == PseudoClass::Not) {
                uint32_t best = 0;
                for (const Selector& inner : pseudo.arguments) {
                    best = std::max(best, computeSpecificity(inner));
                }
                ids += (best >> 20) & 0x3FF;
                classes += (best >> 10) & 0x3FF;
                types += best & 0x3FF;
            } else {
                ++classes;
            }
        }
        if (!compound.tagName.empty()) {
            ++types;
        }
    }
    ids = std::min<uint32_t>(ids, 0x3FF);
    classes = std::min<uint32_t>(classes, 0x3FF);
    types = std::min<uint32_t>(types, 0x3FF);
    return (ids << 20) | (classes << 10) | types;
}

std::optional<SelectorList> SelectorList::parse(std::string_view text, std::string* error) {
    std::string localError;
    Parser parser(text, error ? error : &localError);
    return parser.parseList();
}

} // namespace xgu::css
