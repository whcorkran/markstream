#ifndef INLINE_HPP
#define INLINE_HPP

#include <string>
#include <string_view>
#include <unordered_map>

struct LinkDef;

// Parse and render inline markdown (CommonMark core subset): emphasis,
// strong, code spans, escapes, entities, autolinks, inline HTML, links,
// images, and soft/hard line breaks.
std::string render_inlines_html(
    std::string_view text,
    const std::unordered_map<std::string, LinkDef> *link_defs = nullptr);

// Resolve backslash escapes and HTML entities in a string
std::string resolve_escapes_and_entities(std::string_view text);

#endif // INLINE_HPP
