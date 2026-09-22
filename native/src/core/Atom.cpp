#include "core/Atom.h"

#include <cctype>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace xgu {
namespace {

struct InternTable {
    std::mutex mutex;
    std::unordered_map<std::string_view, std::unique_ptr<Atom::Entry>> map;
};

// Never destroyed: atoms are referenced by long-lived objects and by statics.
InternTable& table() {
    static InternTable* instance = new InternTable();
    return *instance;
}

char asciiLower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

} // namespace

const Atom::Entry* Atom::intern(std::string_view text) {
    InternTable& t = table();
    std::lock_guard lock(t.mutex);
    if (auto it = t.map.find(text); it != t.map.end()) {
        return it->second.get();
    }
    auto entry = std::make_unique<Entry>();
    entry->text.assign(text);
    entry->hash = std::hash<std::string_view>{}(entry->text);
    Entry* raw = entry.get();
    // The key views the entry's own storage, which never moves.
    t.map.emplace(std::string_view(raw->text), std::move(entry));
    return raw;
}

Atom::Atom() : entry_(intern(std::string_view{})) {}

Atom::Atom(std::string_view text) : entry_(intern(text)) {}

Atom Atom::lowered(std::string_view text) {
    bool needsLowering = false;
    for (char c : text) {
        if (c >= 'A' && c <= 'Z') {
            needsLowering = true;
            break;
        }
    }
    if (!needsLowering) {
        return Atom(text);
    }
    std::string lowered;
    lowered.reserve(text.size());
    for (char c : text) {
        lowered.push_back(asciiLower(c));
    }
    return Atom(lowered);
}

bool Atom::equalsIgnoringCase(std::string_view other) const {
    const std::string& text = entry_->text;
    if (text.size() != other.size()) {
        return false;
    }
    for (size_t i = 0; i < text.size(); ++i) {
        if (asciiLower(text[i]) != asciiLower(other[i])) {
            return false;
        }
    }
    return true;
}

size_t Atom::internedCount() {
    InternTable& t = table();
    std::lock_guard lock(t.mutex);
    return t.map.size();
}

} // namespace xgu
