#pragma once

#include <string>
#include <string_view>

namespace xgu::dom {

class Node;

// HTML serialisation (innerHTML / outerHTML). Escapes text and attribute
// values, omits closing tags for void elements and keeps script/style content raw.
std::string serializeNode(const Node& node);
std::string serializeChildren(const Node& node);

// &, <, > (and " in attribute mode) to entities.
std::string escapeHtmlText(std::string_view text);
std::string escapeHtmlAttribute(std::string_view value);

} // namespace xgu::dom
