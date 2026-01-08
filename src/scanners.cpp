#include "scanners.hpp"
#include <algorithm>
#include <cstring>

using namespace scan;

// ============================================================================
// Utility Functions
// ============================================================================

namespace scan {

size_t scan_indentation(const std::string &line, size_t offset,
                        size_t *out_columns) {
  size_t pos = offset;
  size_t columns = 0;

  while (pos < line.size()) {
    if (line[pos] == ' ') {
      columns++;
      pos++;
    } else if (line[pos] == '\t') {
      // Tab advances to next multiple of 4
      columns = (columns + 4) & ~3;
      pos++;
    } else {
      break;
    }
  }

  if (out_columns)
    *out_columns = columns;
  return pos - offset;
}

} // namespace scan

// ============================================================================
// Blank Line Detection
// ============================================================================

bool is_blank_line(const std::string &line, size_t offset) {
  for (size_t i = offset; i < line.size(); i++) {
    if (!is_space(line[i]))
      return false;
  }
  return true;
}

// ============================================================================
// ATX Heading Scanner
// ============================================================================

size_t scan_atx_heading_start(const std::string &line, size_t offset) {
  if (offset >= line.size() || line[offset] != '#')
    return 0;

  size_t count = 0;
  size_t pos = offset;

  // Count '#' characters (max 6)
  while (pos < line.size() && line[pos] == '#' && count < 6) {
    count++;
    pos++;
  }

  if (count == 0)
    return 0;

  // Must be followed by space, tab, or end of line
  if (pos >= line.size() || is_space(line[pos])) {
    return count;
  }

  return 0;
}

size_t scan_atx_heading_end(const std::string &line) {
  if (line.empty())
    return 0;

  // Find end of line (excluding trailing whitespace)
  size_t end = line.size();
  while (end > 0 && is_space(line[end - 1])) {
    end--;
  }

  if (end == 0)
    return 0;

  // Count trailing '#' characters
  size_t hash_end = end;
  while (hash_end > 0 && line[hash_end - 1] == '#') {
    hash_end--;
  }

  size_t hash_count = end - hash_end;
  if (hash_count == 0)
    return 0;

  // The '#' sequence must be preceded by a space (or be the entire content)
  if (hash_end == 0 || is_space(line[hash_end - 1])) {
    return line.size() - hash_end;
  }

  return 0;
}

// ============================================================================
// Setext Heading Scanner
// ============================================================================

size_t scan_setext_heading_line(const std::string &line, size_t offset,
                                char *out_char) {
  if (offset >= line.size())
    return 0;

  char c = line[offset];
  if (c != '=' && c != '-')
    return 0;

  size_t count = 0;
  size_t pos = offset;

  // Count consecutive '=' or '-' characters
  while (pos < line.size() && line[pos] == c) {
    count++;
    pos++;
  }

  // Must have at least 1 character
  if (count == 0)
    return 0;

  // Rest of line must be blank (spaces/tabs only)
  while (pos < line.size()) {
    if (!is_space_or_tab(line[pos]))
      return 0;
    pos++;
  }

  if (out_char)
    *out_char = c;
  return count;
}

// ============================================================================
// Code Fence Scanner
// ============================================================================

size_t scan_open_code_fence(const std::string &line, size_t offset,
                            CodeFenceInfo *out_info) {
  if (offset >= line.size())
    return 0;

  char fence_char = line[offset];
  if (fence_char != '`' && fence_char != '~')
    return 0;

  size_t count = 0;
  size_t pos = offset;

  // Count fence characters
  while (pos < line.size() && line[pos] == fence_char) {
    count++;
    pos++;
  }

  // Need at least 3 fence chars
  if (count < 3)
    return 0;

  // For backtick fences, info string cannot contain backticks
  if (fence_char == '`') {
    for (size_t i = pos; i < line.size(); i++) {
      if (line[i] == '`')
        return 0;
    }
  }

  // Extract info string (trim leading/trailing whitespace)
  std::string info;
  size_t info_start = pos;
  while (info_start < line.size() && is_space_or_tab(line[info_start])) {
    info_start++;
  }
  size_t info_end = line.size();
  while (info_end > info_start && is_space(line[info_end - 1])) {
    info_end--;
  }
  if (info_end > info_start) {
    info = line.substr(info_start, info_end - info_start);
  }

  if (out_info) {
    out_info->fence_char = fence_char;
    out_info->fence_length = count;
    out_info->info = info;
  }

  return count;
}

size_t scan_close_code_fence(const std::string &line, size_t offset,
                             char fence_char, size_t min_length) {
  if (offset >= line.size())
    return 0;

  if (line[offset] != fence_char)
    return 0;

  size_t count = 0;
  size_t pos = offset;

  // Count fence characters
  while (pos < line.size() && line[pos] == fence_char) {
    count++;
    pos++;
  }

  // Must have at least min_length fence chars
  if (count < min_length)
    return 0;

  // Rest of line must be blank
  while (pos < line.size()) {
    if (!is_space(line[pos]))
      return 0;
    pos++;
  }

  return count;
}

// ============================================================================
// Thematic Break Scanner
// ============================================================================

size_t scan_thematic_break(const std::string &line, size_t offset,
                           char *out_char) {
  if (offset >= line.size())
    return 0;

  char delim = '\0';
  size_t count = 0;
  size_t pos = offset;

  while (pos < line.size()) {
    char c = line[pos];

    if (c == '*' || c == '-' || c == '_') {
      if (delim == '\0') {
        delim = c;
      } else if (c != delim) {
        return 0; // Mixed delimiters not allowed
      }
      count++;
    } else if (is_space_or_tab(c)) {
      // Spaces and tabs are allowed between markers
    } else {
      return 0; // Other characters not allowed
    }
    pos++;
  }

  if (count >= 3) {
    if (out_char)
      *out_char = delim;
    return count;
  }

  return 0;
}

// ============================================================================
// Block Quote Scanner
// ============================================================================

size_t scan_block_quote_start(const std::string &line, size_t offset) {
  if (offset >= line.size())
    return 0;

  if (line[offset] == '>') {
    return 1;
  }

  return 0;
}

// ============================================================================
// List Marker Scanner
// ============================================================================

size_t scan_list_marker(const std::string &line, size_t offset,
                        ListMarkerInfo *out_info) {
  if (offset >= line.size())
    return 0;

  size_t pos = offset;
  char marker_char = '\0';
  bool is_ordered = false;
  int start_number = 0;
  size_t marker_start = pos;

  char c = line[pos];

  // Check for bullet marker
  if (c == '-' || c == '*' || c == '+') {
    marker_char = c;
    is_ordered = false;
    pos++;
  }
  // Check for ordered list marker (1-9 digits followed by '.' or ')')
  else if (is_digit(c)) {
    // Count digits (max 9)
    size_t digit_count = 0;
    int number = 0;
    while (pos < line.size() && is_digit(line[pos]) && digit_count < 9) {
      number = number * 10 + (line[pos] - '0');
      digit_count++;
      pos++;
    }

    if (digit_count == 0 || pos >= line.size())
      return 0;

    // Must be followed by '.' or ')'
    if (line[pos] == '.' || line[pos] == ')') {
      marker_char = line[pos];
      is_ordered = true;
      start_number = number;
      pos++;
    } else {
      return 0;
    }
  } else {
    return 0;
  }

  // Marker must be followed by at least one space/tab, or end of line
  if (pos >= line.size()) {
    // Empty list item (just marker at end of line)
    if (out_info) {
      out_info->marker_char = marker_char;
      out_info->is_ordered = is_ordered;
      out_info->start_number = start_number;
      out_info->marker_width = pos - offset;
      out_info->content_offset = pos;
      out_info->padding = 0;
    }
    return pos - offset;
  }

  if (!is_space_or_tab(line[pos]))
    return 0;

  // Count spaces after marker (for determining content offset)
  size_t space_start = pos;
  size_t space_columns = 0;
  while (pos < line.size() && is_space_or_tab(line[pos])) {
    if (line[pos] == ' ') {
      space_columns++;
    } else { // tab
      space_columns = (space_columns + 4) & ~3;
    }
    pos++;

    // CommonMark: if more than 4 spaces, it's indented code
    // content offset is marker + 1 space
    if (space_columns > 4)
      break;
  }

  // Determine actual content offset
  size_t content_offset;
  size_t padding;

  if (pos >= line.size() || is_blank_line(line, pos)) {
    // Blank line after marker - use marker + 1 space
    padding = 1;
    content_offset = space_start + 1;
  } else if (space_columns > 4) {
    // Too much indentation - use marker + 1 space
    padding = 1;
    content_offset = space_start + 1;
  } else {
    // Normal case - content follows spaces
    padding = space_columns;
    content_offset = pos;
  }

  if (out_info) {
    out_info->marker_char = marker_char;
    out_info->is_ordered = is_ordered;
    out_info->start_number = start_number;
    out_info->marker_width = content_offset - offset;
    out_info->content_offset = content_offset;
    out_info->padding = padding;
  }

  return content_offset - offset;
}

// ============================================================================
// HTML Block Scanners
// ============================================================================

// HTML tag names for type 6 (block-level elements)
static const char *html_block_tags[] = {
    "address",    "article",    "aside",      "base",     "basefont",
    "blockquote", "body",       "caption",    "center",   "col",
    "colgroup",   "dd",         "details",    "dialog",   "dir",
    "div",        "dl",         "dt",         "fieldset", "figcaption",
    "figure",     "footer",     "form",       "frame",    "frameset",
    "h1",         "h2",         "h3",         "h4",       "h5",
    "h6",         "head",       "header",     "hr",       "html",
    "iframe",     "legend",     "li",         "link",     "main",
    "menu",       "menuitem",   "nav",        "noframes", "ol",
    "optgroup",   "option",     "p",          "param",    "search",
    "section",    "summary",    "table",      "tbody",    "td",
    "tfoot",      "th",         "thead",      "title",    "tr",
    "track",      "ul",         nullptr};

// Case-insensitive tag name comparison
static bool tag_matches(const std::string &line, size_t pos, const char *tag) {
  size_t tag_len = strlen(tag);
  if (pos + tag_len > line.size())
    return false;

  for (size_t i = 0; i < tag_len; i++) {
    if (to_lower(line[pos + i]) != tag[i])
      return false;
  }

  // Tag must end with space, tab, >, />, or end of string
  if (pos + tag_len >= line.size())
    return true;

  char next = line[pos + tag_len];
  return next == ' ' || next == '\t' || next == '>' || next == '/' ||
         next == '\n' || next == '\r';
}

// Check if tag name is in the block tag list
static bool is_block_tag(const std::string &line, size_t pos) {
  for (const char **tag = html_block_tags; *tag != nullptr; tag++) {
    if (tag_matches(line, pos, *tag))
      return true;
  }
  return false;
}

// Scan an HTML tag name (returns length)
static size_t scan_tag_name(const std::string &line, size_t pos) {
  if (pos >= line.size() || !is_alpha(line[pos]))
    return 0;

  size_t start = pos;
  pos++;

  while (pos < line.size() && (is_alphanumeric(line[pos]) || line[pos] == '-')) {
    pos++;
  }

  return pos - start;
}

// Check for closing tag at position
static bool scan_closing_tag(const std::string &line, size_t pos,
                             size_t *end_pos) {
  if (pos + 1 >= line.size() || line[pos] != '<' || line[pos + 1] != '/')
    return false;

  pos += 2;
  size_t tag_len = scan_tag_name(line, pos);
  if (tag_len == 0)
    return false;

  pos += tag_len;

  // Skip whitespace
  while (pos < line.size() && is_space_or_tab(line[pos]))
    pos++;

  if (pos < line.size() && line[pos] == '>') {
    if (end_pos)
      *end_pos = pos + 1;
    return true;
  }

  return false;
}

HtmlBlockType scan_html_block_start(const std::string &line, size_t offset) {
  if (offset >= line.size() || line[offset] != '<')
    return HtmlBlockType::None;

  size_t pos = offset + 1;

  // Type 2: <!-- (HTML comment)
  if (pos + 2 < line.size() && line[pos] == '!' && line[pos + 1] == '-' &&
      line[pos + 2] == '-') {
    return HtmlBlockType::Type2;
  }

  // Type 3: <? (processing instruction)
  if (pos < line.size() && line[pos] == '?') {
    return HtmlBlockType::Type3;
  }

  // Type 4: <! followed by uppercase letter (declaration)
  if (pos + 1 < line.size() && line[pos] == '!' &&
      line[pos + 1] >= 'A' && line[pos + 1] <= 'Z') {
    return HtmlBlockType::Type4;
  }

  // Type 5: <![CDATA[
  if (pos + 7 < line.size() && line.substr(pos, 8) == "![CDATA[") {
    return HtmlBlockType::Type5;
  }

  // Type 1: <script, <pre, <style, <textarea (case insensitive)
  // Can also be closing tags
  bool is_closing = false;
  if (pos < line.size() && line[pos] == '/') {
    is_closing = true;
    pos++;
  }

  // Type 1 tags
  if (tag_matches(line, pos, "script") || tag_matches(line, pos, "pre") ||
      tag_matches(line, pos, "style") || tag_matches(line, pos, "textarea")) {
    return HtmlBlockType::Type1;
  }

  // Type 6: Block-level HTML elements
  if (is_block_tag(line, pos)) {
    return HtmlBlockType::Type6;
  }

  // Type 7: Other open or closing tags (checked separately)
  if (scan_html_block_start_7(line, offset)) {
    return HtmlBlockType::Type7;
  }

  return HtmlBlockType::None;
}

bool scan_html_block_start_7(const std::string &line, size_t offset) {
  if (offset >= line.size() || line[offset] != '<')
    return false;

  size_t pos = offset + 1;
  bool is_closing = false;

  if (pos < line.size() && line[pos] == '/') {
    is_closing = true;
    pos++;
  }

  // Must have a valid tag name
  size_t tag_len = scan_tag_name(line, pos);
  if (tag_len == 0)
    return false;

  // Don't match type 6 block tags
  if (is_block_tag(line, pos))
    return false;

  pos += tag_len;

  if (is_closing) {
    // Closing tag: optional whitespace, then >
    while (pos < line.size() && is_space_or_tab(line[pos]))
      pos++;

    if (pos >= line.size() || line[pos] != '>')
      return false;

    pos++;
  } else {
    // Open tag: attributes, then > or />
    // Simple attribute scanning (not fully spec-compliant but sufficient)
    while (pos < line.size()) {
      // Skip whitespace
      while (pos < line.size() && is_space_or_tab(line[pos]))
        pos++;

      if (pos >= line.size())
        return false;

      // Check for end of tag
      if (line[pos] == '>') {
        pos++;
        break;
      }
      if (line[pos] == '/' && pos + 1 < line.size() && line[pos + 1] == '>') {
        pos += 2;
        break;
      }

      // Must be start of attribute name
      if (!is_alpha(line[pos]) && line[pos] != '_' && line[pos] != ':')
        return false;

      // Skip attribute name
      while (pos < line.size() &&
             (is_alphanumeric(line[pos]) || line[pos] == '_' ||
              line[pos] == ':' || line[pos] == '.' || line[pos] == '-'))
        pos++;

      // Skip whitespace
      while (pos < line.size() && is_space_or_tab(line[pos]))
        pos++;

      // Check for attribute value
      if (pos < line.size() && line[pos] == '=') {
        pos++;
        // Skip whitespace
        while (pos < line.size() && is_space_or_tab(line[pos]))
          pos++;

        if (pos >= line.size())
          return false;

        // Attribute value
        if (line[pos] == '"') {
          pos++;
          while (pos < line.size() && line[pos] != '"')
            pos++;
          if (pos >= line.size())
            return false;
          pos++;
        } else if (line[pos] == '\'') {
          pos++;
          while (pos < line.size() && line[pos] != '\'')
            pos++;
          if (pos >= line.size())
            return false;
          pos++;
        } else {
          // Unquoted value
          while (pos < line.size() && !is_space(line[pos]) &&
                 line[pos] != '>' && line[pos] != '"' && line[pos] != '\'' &&
                 line[pos] != '=' && line[pos] != '<' && line[pos] != '`')
            pos++;
        }
      }
    }
  }

  // Must be followed by end of line or whitespace only
  while (pos < line.size()) {
    if (!is_space(line[pos]))
      return false;
    pos++;
  }

  return true;
}

bool scan_html_block_end(const std::string &line, size_t offset,
                         HtmlBlockType type) {
  switch (type) {
  case HtmlBlockType::Type1: {
    // Ends when line contains </script>, </pre>, </style>, or </textarea>
    std::string lower;
    lower.reserve(line.size());
    for (char c : line)
      lower += to_lower(c);

    return lower.find("</script>") != std::string::npos ||
           lower.find("</pre>") != std::string::npos ||
           lower.find("</style>") != std::string::npos ||
           lower.find("</textarea>") != std::string::npos;
  }

  case HtmlBlockType::Type2:
    // Ends when line contains -->
    return line.find("-->") != std::string::npos;

  case HtmlBlockType::Type3:
    // Ends when line contains ?>
    return line.find("?>") != std::string::npos;

  case HtmlBlockType::Type4:
    // Ends when line contains >
    return line.find('>') != std::string::npos;

  case HtmlBlockType::Type5:
    // Ends when line contains ]]>
    return line.find("]]>") != std::string::npos;

  case HtmlBlockType::Type6:
  case HtmlBlockType::Type7:
    // Ends on blank line
    return is_blank_line(line, offset);

  case HtmlBlockType::None:
  default:
    return false;
  }
}

// ============================================================================
// Link Label Scanner (for future link reference definitions)
// ============================================================================

size_t scan_link_label(const std::string &line, size_t offset) {
  if (offset >= line.size() || line[offset] != '[')
    return 0;

  size_t pos = offset + 1;
  size_t length = 0;
  bool has_non_space = false;

  while (pos < line.size() && length < 1000) {
    char c = line[pos];

    if (c == ']') {
      if (has_non_space) {
        return pos - offset + 1;
      }
      return 0;
    }

    if (c == '[')
      return 0; // Nested brackets not allowed

    if (c == '\\' && pos + 1 < line.size()) {
      pos += 2;
      length += 2;
      has_non_space = true;
      continue;
    }

    if (!is_space(c))
      has_non_space = true;

    pos++;
    length++;
  }

  return 0;
}
