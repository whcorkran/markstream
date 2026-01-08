#ifndef SCANNERS_HPP
#define SCANNERS_HPP

#include <cstdint>
#include <string>

// Character classification utilities
namespace scan {

inline bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

inline bool is_space_or_tab(char c) { return c == ' ' || c == '\t'; }

inline bool is_line_end(char c) { return c == '\n' || c == '\r' || c == '\0'; }

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

inline bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool is_alphanumeric(char c) { return is_alpha(c) || is_digit(c); }

inline bool is_punct(char c) {
  return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
         (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

inline char to_lower(char c) {
  if (c >= 'A' && c <= 'Z')
    return c + 32;
  return c;
}

// Count leading spaces (up to limit, treating tab as up to 4 spaces)
size_t scan_indentation(const std::string &line, size_t offset,
                        size_t *out_columns);

} // namespace scan

// HTML block types (CommonMark spec)
enum class HtmlBlockType : uint8_t {
  None = 0,
  Type1 = 1, // <script>, <pre>, <style>, <textarea>
  Type2 = 2, // <!-- comment -->
  Type3 = 3, // <? processing instruction ?>
  Type4 = 4, // <!DECLARATION>
  Type5 = 5, // <![CDATA[ ]]>
  Type6 = 6, // Standard HTML tags (div, p, etc.) - ends on blank line
  Type7 = 7  // Other tags - ends on blank line
};

// Code fence info
struct CodeFenceInfo {
  char fence_char;     // '`' or '~'
  size_t fence_length; // Number of fence chars (>= 3)
  std::string info;    // Info string after fence (language, etc.)
};

// List marker info
struct ListMarkerInfo {
  char marker_char;       // '-', '*', '+' for bullet; '.' or ')' for ordered
  bool is_ordered;        // true for ordered lists
  int start_number;       // Starting number for ordered lists (1-9 digits)
  size_t marker_width;    // Total width of marker including trailing space
  size_t content_offset;  // Offset where content begins after marker
  size_t padding;         // Spaces after marker
};

// ATX Heading: 1-6 '#' followed by space/tab or end of line
// Returns heading level (1-6) or 0 if no match
size_t scan_atx_heading_start(const std::string &line, size_t offset);

// ATX Heading closing sequence: optional trailing '#'s
// Returns number of chars in closing sequence (for trimming)
size_t scan_atx_heading_end(const std::string &line);

// Setext heading underline: '=' or '-' (at least 1), optionally followed by spaces
// Returns length matched, sets out_char to '=' or '-'
size_t scan_setext_heading_line(const std::string &line, size_t offset,
                                char *out_char);

// Code fence opener: 3+ '`' or '~', optionally followed by info string
// Returns fence length, fills info struct
size_t scan_open_code_fence(const std::string &line, size_t offset,
                            CodeFenceInfo *out_info);

// Code fence closer: 3+ of same char as opener, nothing else on line
// Returns fence length if closes fence, 0 otherwise
size_t scan_close_code_fence(const std::string &line, size_t offset,
                             char fence_char, size_t min_length);

// Thematic break: 3+ of same char (*, -, _) with optional spaces between
// Returns count of marker chars, sets out_char
size_t scan_thematic_break(const std::string &line, size_t offset,
                           char *out_char);

// Block quote marker: '>' optionally followed by space
// Returns 1 if found (just the '>'), 0 otherwise
size_t scan_block_quote_start(const std::string &line, size_t offset);

// List item marker (bullet or ordered)
// Returns marker width if found, 0 otherwise; fills out_info
size_t scan_list_marker(const std::string &line, size_t offset,
                        ListMarkerInfo *out_info);

// HTML block start detection
// Returns HtmlBlockType (1-7) or None
HtmlBlockType scan_html_block_start(const std::string &line, size_t offset);

// HTML block type 7 specifically (open/close tag not in type 6 list)
// Returns true if matches type 7
bool scan_html_block_start_7(const std::string &line, size_t offset);

// HTML block end conditions (type-specific)
// Returns true if this line ends the HTML block
bool scan_html_block_end(const std::string &line, size_t offset,
                         HtmlBlockType type);

// Blank line detection
bool is_blank_line(const std::string &line, size_t offset);

// Link reference definition (for future use)
// Returns length matched or 0
size_t scan_link_label(const std::string &line, size_t offset);

#endif // SCANNERS_HPP
