#ifndef INLINE_HPP
#define INLINE_HPP

#include <string>
#include <string_view>

// Parse and render inline markdown (CommonMark core subset): emphasis,
// strong, code spans, escapes, entities, autolinks, inline HTML, links,
// images, and soft/hard line breaks.
std::string render_inlines_html(std::string_view text);

#endif // INLINE_HPP
