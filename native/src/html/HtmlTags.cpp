#include "html/HtmlTags.h"

#include <array>
#include <unordered_map>

namespace xgu::html {
namespace {

struct TagInfo {
    HtmlTag tag;
    std::string_view name;
};

constexpr std::array<TagInfo, static_cast<size_t>(HtmlTag::Count)> kTags = {{
    {HtmlTag::Unknown, ""},
    {HtmlTag::Html, "html"},
    {HtmlTag::Head, "head"},
    {HtmlTag::Body, "body"},
    {HtmlTag::Title, "title"},
    {HtmlTag::Meta, "meta"},
    {HtmlTag::Link, "link"},
    {HtmlTag::Style, "style"},
    {HtmlTag::Script, "script"},
    {HtmlTag::Div, "div"},
    {HtmlTag::Span, "span"},
    {HtmlTag::P, "p"},
    {HtmlTag::Img, "img"},
    {HtmlTag::Button, "button"},
    {HtmlTag::Input, "input"},
    {HtmlTag::Textarea, "textarea"},
    {HtmlTag::Label, "label"},
    {HtmlTag::Ul, "ul"},
    {HtmlTag::Li, "li"},
    {HtmlTag::H1, "h1"},
    {HtmlTag::H2, "h2"},
    {HtmlTag::H3, "h3"},
    {HtmlTag::H4, "h4"},
    {HtmlTag::H5, "h5"},
    {HtmlTag::H6, "h6"},
    {HtmlTag::Br, "br"},
    {HtmlTag::A, "a"},
    {HtmlTag::Strong, "strong"},
    {HtmlTag::Em, "em"},
}};

const std::unordered_map<std::string_view, HtmlTag>& nameToTag() {
    static const std::unordered_map<std::string_view, HtmlTag>* map = [] {
        auto* result = new std::unordered_map<std::string_view, HtmlTag>();
        for (const TagInfo& info : kTags) {
            if (!info.name.empty()) {
                result->emplace(info.name, info.tag);
            }
        }
        return result;
    }();
    return *map;
}

} // namespace

HtmlTag tagFromName(std::string_view name) {
    if (name.empty() || name.size() > 16) {
        return HtmlTag::Unknown;
    }
    char lowered[16];
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        lowered[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    const auto& map = nameToTag();
    const auto it = map.find(std::string_view(lowered, name.size()));
    return it == map.end() ? HtmlTag::Unknown : it->second;
}

HtmlTag tagFromAtom(const Atom& name) { return tagFromName(name.view()); }

std::string_view tagName(HtmlTag tag) {
    const size_t index = static_cast<size_t>(tag);
    return index < kTags.size() ? kTags[index].name : std::string_view{};
}

bool isVoidTag(HtmlTag tag) {
    switch (tag) {
    case HtmlTag::Img:
    case HtmlTag::Input:
    case HtmlTag::Br:
    case HtmlTag::Meta:
    case HtmlTag::Link:
        return true;
    default:
        return false;
    }
}

bool isRawTextTag(HtmlTag tag) { return tag == HtmlTag::Script || tag == HtmlTag::Style; }

bool isNonRenderedTag(HtmlTag tag) {
    switch (tag) {
    case HtmlTag::Head:
    case HtmlTag::Meta:
    case HtmlTag::Link:
    case HtmlTag::Style:
    case HtmlTag::Script:
    case HtmlTag::Title:
        return true;
    default:
        return false;
    }
}

} // namespace xgu::html
