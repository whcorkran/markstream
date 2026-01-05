#include "scanners.hpp"
using namespace validation;

size_t scan_block_delimiter(const std::string &line, size_t offset,
                            BlockType block, char *out_delim_char) {
  if (offset >= line.size())
    return 0;

  BlockSpec spec = get_spec(block);

  // Find which delimiter char is present
  char found_delim = '\0';
  if (line[offset] == spec.delim_char) {
    found_delim = spec.delim_char;
  } else {
    for (const char *alt = spec.alternate_delims; *alt != '\0'; ++alt) {
      if (line[offset] == *alt) {
        found_delim = *alt;
        break;
      }
    }
  }

  if (found_delim == '\0')
    return 0;

  // Count consecutive occurrences
  size_t count = 0;
  size_t pos = offset;
  while (pos < line.size() && line[pos] == found_delim) {
    if (spec.max_repeat > 0 && count >= spec.max_repeat)
      break;
    count++;
    pos++;
  }

  if (count >= spec.min_repeat) {
    *out_delim_char = found_delim;
    return count;
  }
  return 0;
}

// ATX heading: # chars (1-6) followed by space or EOL
size_t scan_atx_heading_start(const std::string &line, size_t offset) {
  char delim;
  size_t count = scan_block_delimiter(line, offset, BlockType::Heading, &delim);

  if (count > 0) {
    size_t after = offset + count;
    if (after >= line.size() || is_space(line[after]))
      return count;
  }
  return 0;
}

// Thematic break: *, -, or _ (min 3) with optional spaces between
size_t scan_thematic_break(const std::string &line, size_t offset,
                           char *out_delim_char) {
  size_t pos = offset;
  char delim = '\0';
  size_t count = 0;

  while (pos < line.size()) {
    char ch = line[pos];
    if (ch == '*' || ch == '-' || ch == '_') {
      if (delim == '\0') {
        delim = ch;
      } else if (ch != delim) {
        return 0; // Mixed delimiters
      }
      count++;
    } else if (!is_space(ch)) {
      return 0; // Non-space, non-delimiter
    }
    pos++;
  }

  if (count >= 3) {
    *out_delim_char = delim;
    return count;
  }
  return 0;
}

size_t scan_html_block_start(const std::string &line, size_t offset) {
  return 0; // TODO
}
size_t scan_html_block_start_7(const std::string &line, size_t offset) {
  return 0; // TODO
}
size_t scan_html_block_end_N(const std::string &line, size_t offset) {
  return 0; // TODO
}
