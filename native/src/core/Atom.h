#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace xgu {

// Interned string. Equality and hashing are pointer operations, which is what
// makes tag/attribute/class/id comparisons cheap in the DOM and the selector
// matcher. Atoms live for the lifetime of the process.
class Atom {
public:
    Atom();
    explicit Atom(std::string_view text);

    // Interns the ASCII-lowercased form (HTML tag and attribute names).
    static Atom lowered(std::string_view text);

    const std::string& string() const { return entry_->text; }
    const char* c_str() const { return entry_->text.c_str(); }
    std::string_view view() const { return entry_->text; }
    bool empty() const { return entry_->text.empty(); }
    size_t size() const { return entry_->text.size(); }
    size_t hash() const { return entry_->hash; }

    bool operator==(const Atom& other) const { return entry_ == other.entry_; }
    bool operator!=(const Atom& other) const { return entry_ != other.entry_; }
    // Deterministic but arbitrary order, for sorted containers.
    bool operator<(const Atom& other) const { return entry_->text < other.entry_->text; }

    bool equalsIgnoringCase(std::string_view other) const;

    static size_t internedCount();

    // Interned storage. Public so the intern table can name it; never created
    // outside Atom::intern.
    struct Entry {
        std::string text;
        size_t hash;
    };

private:
    explicit Atom(const Entry* entry) : entry_(entry) {}
    static const Entry* intern(std::string_view text);

    const Entry* entry_;
};

} // namespace xgu

template <>
struct std::hash<xgu::Atom> {
    size_t operator()(const xgu::Atom& atom) const noexcept { return atom.hash(); }
};
