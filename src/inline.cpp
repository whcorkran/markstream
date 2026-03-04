#include "inline.hpp"
#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class InlineKind : uint8_t {
  Text,
  Code,
  Html,
  Softbreak,
  Linebreak,
  Emph,
  Strong,
  Link,
  Image,
};

struct InlineNode {
  InlineKind kind = InlineKind::Text;
  std::string literal;
  std::string url;
  std::string title;
  std::vector<InlineNode> children;
};

struct Delimiter {
  size_t node_index = 0;
  size_t length = 0;
  char delim_char = '\0';
  bool can_open = false;
  bool can_close = false;
  bool active = true;
};

inline bool is_ascii_space(char c) {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' ||
         c == '\v';
}

inline bool is_ascii_punct(char c) {
  return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
         (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

inline bool is_escaped(std::string_view s, size_t pos) {
  if (pos == 0) {
    return false;
  }
  size_t backslashes = 0;
  while (pos > 0 && s[--pos] == '\\') {
    backslashes++;
  }
  return (backslashes % 2) == 1;
}

std::string escape_html(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}


std::string encode_utf8(uint32_t codepoint) {
  std::string out;
  if (codepoint <= 0x7F) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return out;
}

#include "entities.inc"

// Forward declarations
std::optional<std::string> decode_entity(std::string_view s, size_t start,
                                         size_t *consumed);
std::string normalize_url(std::string_view raw);
std::string normalize_title(std::string_view raw);

static uint32_t inline_unicode_tolower(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + 32;
  if (cp >= 0xC0 && cp <= 0xD6) return cp + 32;
  if (cp >= 0xD8 && cp <= 0xDE) return cp + 32;
  if (cp >= 0x391 && cp <= 0x3A1) return cp + 32;
  if (cp >= 0x3A3 && cp <= 0x3A9) return cp + 32;
  if (cp >= 0x410 && cp <= 0x42F) return cp + 32;
  return cp;
}

static std::pair<uint32_t, size_t> inline_decode_utf8(std::string_view s, size_t pos) {
  unsigned char c = static_cast<unsigned char>(s[pos]);
  if (c < 0x80) return {c, 1};
  if ((c & 0xE0) == 0xC0 && pos + 1 < s.size()) {
    return {(c & 0x1F) << 6 | (static_cast<unsigned char>(s[pos+1]) & 0x3F), 2};
  }
  if ((c & 0xF0) == 0xE0 && pos + 2 < s.size()) {
    return {(c & 0x0F) << 12 |
        (static_cast<unsigned char>(s[pos+1]) & 0x3F) << 6 |
        (static_cast<unsigned char>(s[pos+2]) & 0x3F), 3};
  }
  if ((c & 0xF8) == 0xF0 && pos + 3 < s.size()) {
    return {(c & 0x07) << 18 |
        (static_cast<unsigned char>(s[pos+1]) & 0x3F) << 12 |
        (static_cast<unsigned char>(s[pos+2]) & 0x3F) << 6 |
        (static_cast<unsigned char>(s[pos+3]) & 0x3F), 4};
  }
  return {c, 1};
}

std::string normalize_label(std::string_view label) {
  std::string result;
  size_t start = 0;
  while (start < label.size() &&
         (label[start] == ' ' || label[start] == '\t' || label[start] == '\n'))
    start++;
  size_t end = label.size();
  while (end > start &&
         (label[end - 1] == ' ' || label[end - 1] == '\t' ||
          label[end - 1] == '\n'))
    end--;

  bool in_space = false;
  for (size_t i = start; i < end;) {
    char c = label[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!in_space) {
        result += ' ';
        in_space = true;
      }
      i++;
    } else {
      auto [cp, bytes] = inline_decode_utf8(label, i);
      uint32_t lower = inline_unicode_tolower(cp);
      result += encode_utf8(lower);
      in_space = false;
      i += bytes;
    }
  }
  return result;
}

std::optional<std::string> decode_entity(std::string_view s, size_t start,
                                         size_t *consumed) {
  if (start >= s.size() || s[start] != '&') {
    return std::nullopt;
  }

  size_t semi = s.find(';', start + 1);
  if (semi == std::string_view::npos) {
    return std::nullopt;
  }

  std::string_view name = s.substr(start + 1, semi - (start + 1));
  if (name.empty()) {
    return std::nullopt;
  }

  if (name[0] == '#') {
    bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
    size_t pos = hex ? 2 : 1;
    if (pos >= name.size()) {
      return std::nullopt;
    }
    uint32_t cp = 0;
    for (; pos < name.size(); pos++) {
      char c = name[pos];
      int v = -1;
      if (c >= '0' && c <= '9') {
        v = c - '0';
      } else if (hex && c >= 'a' && c <= 'f') {
        v = 10 + (c - 'a');
      } else if (hex && c >= 'A' && c <= 'F') {
        v = 10 + (c - 'A');
      }
      if (v < 0) {
        return std::nullopt;
      }
      cp = cp * (hex ? 16u : 10u) + static_cast<uint32_t>(v);
      if (cp > 0x10FFFF) {
        return std::nullopt;
      }
    }
    // Map 0, surrogates, and out-of-range to replacement character
    if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
      cp = 0xFFFD;
    }
    *consumed = semi - start + 1;
    return encode_utf8(cp);
  }

  // Look up named entity in full HTML5 table
  auto result = lookup_entity(name);
  if (!result) {
    return std::nullopt;
  }
  *consumed = semi - start + 1;
  return result;
}

void percent_encode_byte(std::string &out, unsigned char c) {
  static const char hex[] = "0123456789ABCDEF";
  out += '%';
  out += hex[c >> 4];
  out += hex[c & 0x0F];
}

bool is_url_safe(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' ||
         c == '_' || c == '~' || c == ':' || c == '/' || c == '?' ||
         c == '#' || c == '[' || c == ']' || c == '@' || c == '!' ||
         c == '$' || c == '&' || c == '\'' || c == '(' || c == ')' ||
         c == '*' || c == '+' || c == ',' || c == ';' || c == '=';
}

std::string normalize_url(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); i++) {
    char c = raw[i];
    if (c == '\\' && i + 1 < raw.size() && is_ascii_punct(raw[i + 1])) {
      char next = raw[i + 1];
      i++;
      if (is_url_safe(next)) {
        out += next;
      } else {
        percent_encode_byte(out, static_cast<unsigned char>(next));
      }
    } else if (c == '\\') {
      // Unescaped backslash — percent-encode it
      percent_encode_byte(out, static_cast<unsigned char>(c));
    } else if (c == '&') {
      size_t consumed = 0;
      auto decoded = decode_entity(raw, i, &consumed);
      if (decoded) {
        for (unsigned char dc : *decoded) {
          if (dc > 0x7F) {
            percent_encode_byte(out, dc);
          } else if (is_url_safe(static_cast<char>(dc)) ||
                     static_cast<char>(dc) == '%') {
            out += static_cast<char>(dc);
          } else {
            percent_encode_byte(out, dc);
          }
        }
        i += consumed - 1;
      } else {
        out += '&';
      }
    } else if (c == '%' && i + 2 < raw.size() &&
               std::isxdigit(static_cast<unsigned char>(raw[i + 1])) &&
               std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) {
      out += raw[i];
      out += raw[i + 1];
      out += raw[i + 2];
      i += 2;
    } else if (static_cast<unsigned char>(c) > 0x7F) {
      percent_encode_byte(out, static_cast<unsigned char>(c));
    } else if (c == ' ' || c == '"') {
      percent_encode_byte(out, static_cast<unsigned char>(c));
    } else {
      out += c;
    }
  }
  return out;
}

std::string normalize_title(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); i++) {
    char c = raw[i];
    if (c == '\\' && i + 1 < raw.size() && is_ascii_punct(raw[i + 1])) {
      out += raw[i + 1];
      i++;
    } else if (c == '&') {
      size_t consumed = 0;
      auto decoded = decode_entity(raw, i, &consumed);
      if (decoded) {
        out += *decoded;
        i += consumed - 1;
      } else {
        out += '&';
      }
    } else {
      out += c;
    }
  }
  return out;
}

bool is_uri_autolink(std::string_view s) {
  size_t colon = s.find(':');
  if (colon == std::string_view::npos || colon < 2 || colon > 32) {
    return false;
  }
  if (!std::isalpha(static_cast<unsigned char>(s[0]))) {
    return false;
  }
  for (size_t i = 1; i < colon; i++) {
    char c = s[i];
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' ||
          c == '.')) {
      return false;
    }
  }
  for (char c : s.substr(colon + 1)) {
    if (is_ascii_space(c) || c == '<' || c == '>') {
      return false;
    }
  }
  return true;
}

bool is_email_autolink(std::string_view s) {
  size_t at = s.find('@');
  if (at == std::string_view::npos || at == 0 || at + 1 >= s.size()) {
    return false;
  }
  auto is_label = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' ||
           c == '+' || c == '-';
  };
  for (size_t i = 0; i < at; i++) {
    if (!is_label(s[i]))
      return false;
  }
  bool has_dot = false;
  for (size_t i = at + 1; i < s.size(); i++) {
    if (!is_label(s[i]))
      return false;
    if (s[i] == '.')
      has_dot = true;
  }
  return has_dot;
}

bool is_inline_html_tag(std::string_view s) {
  if (s.size() < 3 || s.front() != '<' || s.back() != '>') {
    return false;
  }
  if (s.find('\n') != std::string_view::npos || s.find('\r') != std::string_view::npos) {
    return false;
  }
  char first = s[1];
  return std::isalpha(static_cast<unsigned char>(first)) || first == '/' ||
         first == '!' || first == '?';
}

bool is_delimiter_text_node(const std::vector<Delimiter> &delims, size_t idx) {
  for (const Delimiter &d : delims) {
    if (d.active && d.node_index == idx) {
      return true;
    }
  }
  return false;
}

void append_text(std::vector<InlineNode> *nodes,
                 const std::vector<Delimiter> &delims, std::string_view text) {
  if (text.empty()) {
    return;
  }
  if (!nodes->empty() && (*nodes)[nodes->size() - 1].kind == InlineKind::Text) {
    if (is_delimiter_text_node(delims, nodes->size() - 1)) {
      InlineNode node;
      node.kind = InlineKind::Text;
      node.literal.assign(text);
      nodes->push_back(std::move(node));
      return;
    }
    (*nodes)[nodes->size() - 1].literal.append(text);
    return;
  }
  InlineNode node;
  node.kind = InlineKind::Text;
  node.literal.assign(text);
  nodes->push_back(std::move(node));
}

bool trim_trailing_spaces(std::vector<InlineNode> *nodes, size_t min_count) {
  size_t total_removed = 0;
  while (!nodes->empty()) {
    InlineNode &last = (*nodes)[nodes->size() - 1];
    if (last.kind != InlineKind::Text || last.literal.empty()) {
      break;
    }
    if (last.literal.back() != ' ') {
      break;
    }
    last.literal.pop_back();
    total_removed++;
    if (last.literal.empty()) {
      nodes->pop_back();
    }
  }
  return total_removed >= min_count;
}

bool is_left_flanking(char before, char after) {
  bool after_space = (after == '\0') || is_ascii_space(after);
  bool after_punct = (after != '\0') && is_ascii_punct(after);
  bool before_space = (before == '\0') || is_ascii_space(before);
  bool before_punct = (before != '\0') && is_ascii_punct(before);
  return !after_space && (!after_punct || before_space || before_punct);
}

bool is_right_flanking(char before, char after) {
  bool after_space = (after == '\0') || is_ascii_space(after);
  bool after_punct = (after != '\0') && is_ascii_punct(after);
  bool before_space = (before == '\0') || is_ascii_space(before);
  bool before_punct = (before != '\0') && is_ascii_punct(before);
  return !before_space && (!before_punct || after_space || after_punct);
}

void shift_delimiter_indices(std::vector<Delimiter> *delims, size_t index,
                             ptrdiff_t delta) {
  for (Delimiter &d : *delims) {
    if (!d.active) {
      continue;
    }
    if (d.node_index > index || (delta > 0 && d.node_index >= index)) {
      d.node_index = static_cast<size_t>(static_cast<ptrdiff_t>(d.node_index) +
                                         delta);
    }
  }
}

void remove_node(std::vector<InlineNode> *nodes, std::vector<Delimiter> *delims,
                 size_t index) {
  nodes->erase(nodes->begin() + static_cast<long>(index));
  for (Delimiter &d : *delims) {
    if (!d.active)
      continue;
    if (d.node_index == index) {
      d.active = false;
      continue;
    }
    if (d.node_index > index) {
      d.node_index--;
    }
  }
}

void process_emphasis(std::vector<InlineNode> *nodes, std::vector<Delimiter> *delims) {
  for (size_t ci = 0; ci < delims->size(); ci++) {
    bool keep_trying = true;
    while (keep_trying) {
      keep_trying = false;

      if (ci >= delims->size())
        break;
      Delimiter &closer = (*delims)[ci];
      if (!closer.active || !closer.can_close || closer.length == 0)
        break;

      ssize_t opener_i = -1;
      for (ssize_t oi = static_cast<ssize_t>(ci) - 1; oi >= 0; oi--) {
        Delimiter &opener = (*delims)[static_cast<size_t>(oi)];
        if (!opener.active || !opener.can_open || opener.length == 0)
          continue;
        if (opener.delim_char != closer.delim_char)
          continue;
        if ((opener.can_close || closer.can_open) &&
            (opener.length + closer.length) % 3 == 0 &&
            (opener.length % 3 != 0 || closer.length % 3 != 0))
          continue;
        opener_i = oi;
        break;
      }

      if (opener_i < 0)
        break;

      Delimiter &opener = (*delims)[static_cast<size_t>(opener_i)];
      size_t use = (opener.length >= 2 && closer.length >= 2) ? 2 : 1;

      InlineNode &opener_text = (*nodes)[opener.node_index];
      InlineNode &closer_text = (*nodes)[closer.node_index];
      opener_text.literal.resize(opener_text.literal.size() - use);
      closer_text.literal.erase(0, use);
      opener.length -= use;
      closer.length -= use;

      size_t open_idx = opener.node_index;
      size_t close_idx = closer.node_index;

      // Deactivate delimiters between opener and closer
      for (size_t di = static_cast<size_t>(opener_i) + 1; di < ci; di++) {
        (*delims)[di].active = false;
      }

      InlineNode wrapper;
      wrapper.kind = (use == 2) ? InlineKind::Strong : InlineKind::Emph;
      if (close_idx > open_idx + 1) {
        auto begin = nodes->begin() + static_cast<long>(open_idx + 1);
        auto end = nodes->begin() + static_cast<long>(close_idx);
        wrapper.children.assign(std::make_move_iterator(begin),
                                std::make_move_iterator(end));
        nodes->erase(begin, end);
        ptrdiff_t removed = static_cast<ptrdiff_t>(close_idx - (open_idx + 1));
        shift_delimiter_indices(delims, open_idx + 1, -removed);
        close_idx = open_idx + 1;
      }

      nodes->insert(nodes->begin() + static_cast<long>(open_idx + 1),
                    std::move(wrapper));
      shift_delimiter_indices(delims, open_idx + 1, 1);
      close_idx++;

      // Update closer's node_index after shifts
      (*delims)[ci].node_index = close_idx;

      if (opener.length == 0) {
        remove_node(nodes, delims, open_idx);
        close_idx--;
        (*delims)[ci].node_index = close_idx;
      }
      if ((*delims)[ci].length == 0) {
        remove_node(nodes, delims, close_idx);
      } else {
        // Closer still has remaining delimiters — re-examine it
        keep_trying = true;
      }
    }
  }
}

std::vector<InlineNode> parse_inlines(
    std::string_view input, bool allow_links,
    const std::unordered_map<std::string, LinkDef> *link_defs = nullptr);

size_t parse_link_label_end(std::string_view s, size_t open_bracket) {
  size_t depth = 0;
  for (size_t i = open_bracket; i < s.size(); i++) {
    char c = s[i];
    if (c == '\\') {
      if (i + 1 < s.size())
        i++;
      continue;
    }
    if (c == '`') {
      // Skip code spans — ] inside code spans doesn't count
      size_t tick_start = i;
      while (i < s.size() && s[i] == '`')
        i++;
      size_t tick_count = i - tick_start;
      bool found = false;
      size_t j = i;
      while (j < s.size()) {
        if (s[j] != '`') {
          j++;
          continue;
        }
        size_t k = j;
        while (k < s.size() && s[k] == '`')
          k++;
        if (k - j == tick_count) {
          i = k - 1; // will be incremented by for loop
          found = true;
          break;
        }
        j = k;
      }
      if (!found) {
        i = tick_start; // will be incremented by for loop
      }
      continue;
    }
    if (c == '[') {
      depth++;
    } else if (c == ']') {
      depth--;
      if (depth == 0) {
        return i;
      }
    }
  }
  return std::string_view::npos;
}

bool parse_link_destination(std::string_view s, size_t *pos, std::string *out) {
  size_t i = *pos;
  if (i >= s.size())
    return false;

  if (s[i] == '<') {
    i++;
    size_t start = i;
    while (i < s.size()) {
      if (s[i] == '\\') {
        i += (i + 1 < s.size()) ? 2 : 1;
        continue;
      }
      if (s[i] == '>') {
        *out = std::string(s.substr(start, i - start));
        *pos = i + 1;
        return true;
      }
      if (s[i] == '\n' || s[i] == '<') {
        return false;
      }
      i++;
    }
    return false;
  }

  size_t start = i;
  int paren_depth = 0;
  while (i < s.size()) {
    char c = s[i];
    if (c == '\\') {
      i += (i + 1 < s.size()) ? 2 : 1;
      continue;
    }
    if (is_ascii_space(c)) {
      break;
    }
    if (c == '(') {
      paren_depth++;
      if (paren_depth > 32) {
        return false;
      }
    } else if (c == ')') {
      if (paren_depth == 0)
        break;
      paren_depth--;
    }
    i++;
  }
  if (i == start) {
    return false;
  }
  *out = std::string(s.substr(start, i - start));
  *pos = i;
  return true;
}

bool parse_link_title(std::string_view s, size_t *pos, std::string *out) {
  if (*pos >= s.size()) {
    return false;
  }
  char opener = s[*pos];
  char closer = opener == '(' ? ')' : opener;
  if (opener != '\'' && opener != '"' && opener != '(') {
    return false;
  }
  size_t i = *pos + 1;
  size_t start = i;
  while (i < s.size()) {
    if (s[i] == '\\') {
      i += (i + 1 < s.size()) ? 2 : 1;
      continue;
    }
    if (s[i] == closer) {
      *out = std::string(s.substr(start, i - start));
      *pos = i + 1;
      return true;
    }
    i++;
  }
  return false;
}

bool parse_inline_link(std::string_view s, size_t after_label, size_t *end_pos,
                       std::string *url, std::string *title) {
  size_t i = after_label;
  if (i >= s.size() || s[i] != '(') {
    return false;
  }
  i++;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) {
    i++;
  }

  // Empty destination: [link]() or [link]( )
  if (i < s.size() && s[i] == ')') {
    *url = "";
  } else if (!parse_link_destination(s, &i, url)) {
    return false;
  }

  size_t before_title = i;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) {
    i++;
  }

  if (i < s.size()) {
    size_t title_pos = i;
    std::string parsed_title;
    if (parse_link_title(s, &title_pos, &parsed_title)) {
      i = title_pos;
      while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) {
        i++;
      }
      *title = std::move(parsed_title);
    } else {
      i = before_title;
    }
  }

  if (i >= s.size() || s[i] != ')') {
    return false;
  }

  *end_pos = i + 1;
  return true;
}

std::vector<InlineNode> parse_inlines(
    std::string_view input, bool allow_links,
    const std::unordered_map<std::string, LinkDef> *link_defs) {
  std::vector<InlineNode> nodes;
  std::vector<Delimiter> delimiters;

  size_t i = 0;
  while (i < input.size()) {
    char c = input[i];

    if (c == '*' || c == '_') {
      size_t run_start = i;
      while (i < input.size() && input[i] == c) {
        i++;
      }
      size_t run_len = i - run_start;
      char before = (run_start == 0) ? '\0' : input[run_start - 1];
      char after = (i < input.size()) ? input[i] : '\0';

      bool left = is_left_flanking(before, after);
      bool right = is_right_flanking(before, after);
      bool can_open = left;
      bool can_close = right;
      if (c == '_') {
        can_open = left && (!right || is_ascii_punct(before));
        can_close = right && (!left || is_ascii_punct(after));
      }

      InlineNode t;
      t.kind = InlineKind::Text;
      t.literal.assign(run_len, c);
      nodes.push_back(std::move(t));
      delimiters.push_back(
          Delimiter{nodes.size() - 1, run_len, c, can_open, can_close, true});
      continue;
    }

    if (c == '`') {
      size_t open_start = i;
      while (i < input.size() && input[i] == '`') {
        i++;
      }
      size_t tick_count = i - open_start;
      size_t j = i;
      size_t close_start = std::string_view::npos;
      while (j < input.size()) {
        if (input[j] != '`') {
          j++;
          continue;
        }
        size_t k = j;
        while (k < input.size() && input[k] == '`') {
          k++;
        }
        if (k - j == tick_count) {
          close_start = j;
          i = k;
          break;
        }
        j = k;
      }

      if (close_start == std::string_view::npos) {
        append_text(&nodes, delimiters, input.substr(open_start, tick_count));
      } else {
        std::string code(input.substr(open_start + tick_count,
                                      close_start - (open_start + tick_count)));
        bool has_nonspace = false;
        for (char &ch : code) {
          if (ch == '\n' || ch == '\r') {
            ch = ' ';
          }
          if (ch != ' ') {
            has_nonspace = true;
          }
        }
        if (has_nonspace && code.size() >= 2 && code.front() == ' ' &&
            code.back() == ' ') {
          code.erase(code.begin());
          code.pop_back();
        }

        InlineNode node;
        node.kind = InlineKind::Code;
        node.literal = std::move(code);
        nodes.push_back(std::move(node));
      }
      continue;
    }

    if (c == '\\') {
      if (i + 1 < input.size()) {
        char n = input[i + 1];
        if (is_ascii_punct(n)) {
          append_text(&nodes, delimiters, input.substr(i + 1, 1));
          i += 2;
          continue;
        }
        if (n == '\n' || n == '\r') {
          InlineNode br;
          br.kind = InlineKind::Linebreak;
          nodes.push_back(std::move(br));
          i += 2;
          if (n == '\r' && i < input.size() && input[i] == '\n') {
            i++;
          }
          continue;
        }
      }
      append_text(&nodes, delimiters, "\\");
      i++;
      continue;
    }

    if (c == '&') {
      size_t consumed = 0;
      std::optional<std::string> decoded = decode_entity(input, i, &consumed);
      if (decoded) {
        append_text(&nodes, delimiters, *decoded);
        i += consumed;
      } else {
        append_text(&nodes, delimiters, "&");
        i++;
      }
      continue;
    }

    if (c == '<') {
      size_t gt = input.find('>', i + 1);
      if (gt != std::string_view::npos) {
        std::string_view inner = input.substr(i + 1, gt - (i + 1));
        if (is_uri_autolink(inner) || is_email_autolink(inner)) {
          InlineNode link;
          link.kind = InlineKind::Link;
          // Autolinks: percent-encode unsafe chars but don't process
          // backslash escapes or entities
          {
            std::string raw_url =
                is_email_autolink(inner)
                    ? ("mailto:" + std::string(inner))
                    : std::string(inner);
            std::string encoded_url;
            for (size_t ai = 0; ai < raw_url.size(); ai++) {
              unsigned char ac = static_cast<unsigned char>(raw_url[ai]);
              if (ac > 0x7F || ac == '"' || ac == '\\') {
                percent_encode_byte(encoded_url, ac);
              } else {
                encoded_url += static_cast<char>(ac);
              }
            }
            link.url = std::move(encoded_url);
          }
          InlineNode txt;
          txt.kind = InlineKind::Text;
          txt.literal = std::string(inner);
          link.children.push_back(std::move(txt));
          nodes.push_back(std::move(link));
          i = gt + 1;
          continue;
        }

        std::string_view raw = input.substr(i, gt - i + 1);
        if (is_inline_html_tag(raw)) {
          InlineNode html;
          html.kind = InlineKind::Html;
          html.literal = std::string(raw);
          nodes.push_back(std::move(html));
          i = gt + 1;
          continue;
        }
      }
      append_text(&nodes, delimiters, "<");
      i++;
      continue;
    }

    if (c == '[' && !allow_links) {
      append_text(&nodes, delimiters, "[");
      i++;
      continue;
    }

    if ((c == '!' && i + 1 < input.size() && input[i + 1] == '[') ||
        (c == '[' && allow_links)) {
      bool is_image = (c == '!');
      size_t open = is_image ? i + 1 : i;
      size_t close = parse_link_label_end(input, open);
      if (close == std::string_view::npos) {
        append_text(&nodes, delimiters, is_image ? "!" : "[");
        i += is_image ? 1 : 1;
        continue;
      }

      std::string_view label =
          input.substr(open + 1, close - (open + 1));
      size_t end_pos = 0;
      std::string url;
      std::string title;
      if (parse_inline_link(input, close + 1, &end_pos, &url, &title)) {
        InlineNode node;
        node.kind = is_image ? InlineKind::Image : InlineKind::Link;
        node.url = normalize_url(url);
        node.title = normalize_title(title);
        node.children = parse_inlines(label, !allow_links, link_defs);
        nodes.push_back(std::move(node));
        i = end_pos;
        continue;
      }

      // Try reference links if link_defs available
      if (link_defs) {
        // Try full reference: [text][label]
        if (close + 1 < input.size() && input[close + 1] == '[') {
          size_t ref_close = parse_link_label_end(input, close + 1);
          if (ref_close != std::string_view::npos) {
            std::string_view ref_label =
                input.substr(close + 2, ref_close - close - 2);
            std::string key = normalize_label(ref_label);
            auto it = link_defs->find(key);
            if (it != link_defs->end()) {
              InlineNode node;
              node.kind = is_image ? InlineKind::Image : InlineKind::Link;
              node.url = normalize_url(it->second.url);
              node.title = normalize_title(it->second.title);
              node.children = parse_inlines(label, !allow_links, link_defs);
              nodes.push_back(std::move(node));
              i = ref_close + 1;
              continue;
            }
          }
        }
        // Try collapsed reference: [text][]
        if (close + 2 < input.size() && input[close + 1] == '[' &&
            input[close + 2] == ']') {
          std::string key = normalize_label(label);
          auto it = link_defs->find(key);
          if (it != link_defs->end()) {
            InlineNode node;
            node.kind = is_image ? InlineKind::Image : InlineKind::Link;
            node.url = normalize_url(it->second.url);
            node.title = normalize_title(it->second.title);
            node.children = parse_inlines(label, !allow_links, link_defs);
            nodes.push_back(std::move(node));
            i = close + 3;
            continue;
          }
        }
        // Try shortcut reference: [text]
        {
          std::string key = normalize_label(label);
          auto it = link_defs->find(key);
          if (it != link_defs->end()) {
            InlineNode node;
            node.kind = is_image ? InlineKind::Image : InlineKind::Link;
            node.url = normalize_url(it->second.url);
            node.title = normalize_title(it->second.title);
            node.children = parse_inlines(label, !allow_links, link_defs);
            nodes.push_back(std::move(node));
            i = close + 1;
            continue;
          }
        }
      }

      append_text(&nodes, delimiters, is_image ? "![" : "[");
      i += is_image ? 2 : 1;
      continue;
    }

    if (c == '\n' || c == '\r') {
      bool hard = trim_trailing_spaces(&nodes, 2);
      InlineNode br;
      br.kind = hard ? InlineKind::Linebreak : InlineKind::Softbreak;
      nodes.push_back(std::move(br));
      i++;
      if (c == '\r' && i < input.size() && input[i] == '\n') {
        i++;
      }
      continue;
    }

    size_t start = i;
    while (i < input.size()) {
      char ch = input[i];
      if (ch == '*' || ch == '_' || ch == '`' || ch == '\\' || ch == '&' ||
          ch == '<' || ch == '\n' || ch == '\r' || ch == '[' || ch == '!') {
        break;
      }
      i++;
    }
    append_text(&nodes, delimiters, input.substr(start, i - start));
  }

  process_emphasis(&nodes, &delimiters);
  return nodes;
}

std::string collect_text(const std::vector<InlineNode> &nodes) {
  std::string out;
  for (const InlineNode &n : nodes) {
    switch (n.kind) {
    case InlineKind::Text:
    case InlineKind::Code:
    case InlineKind::Html:
      out += n.literal;
      break;
    case InlineKind::Softbreak:
    case InlineKind::Linebreak:
      out.push_back(' ');
      break;
    case InlineKind::Emph:
    case InlineKind::Strong:
    case InlineKind::Link:
    case InlineKind::Image:
      out += collect_text(n.children);
      break;
    }
  }
  return out;
}

std::string render_nodes_html(const std::vector<InlineNode> &nodes) {
  std::string out;
  for (const InlineNode &n : nodes) {
    switch (n.kind) {
    case InlineKind::Text:
      out += escape_html(n.literal);
      break;
    case InlineKind::Code:
      out += "<code>";
      out += escape_html(n.literal);
      out += "</code>";
      break;
    case InlineKind::Html:
      out += n.literal;
      break;
    case InlineKind::Softbreak:
      out.push_back('\n');
      break;
    case InlineKind::Linebreak:
      out += "<br />\n";
      break;
    case InlineKind::Emph:
      out += "<em>";
      out += render_nodes_html(n.children);
      out += "</em>";
      break;
    case InlineKind::Strong:
      out += "<strong>";
      out += render_nodes_html(n.children);
      out += "</strong>";
      break;
    case InlineKind::Link:
      out += "<a href=\"";
      out += escape_html(n.url);
      out += "\"";
      if (!n.title.empty()) {
        out += " title=\"";
        out += escape_html(n.title);
        out += "\"";
      }
      out += ">";
      out += render_nodes_html(n.children);
      out += "</a>";
      break;
    case InlineKind::Image: {
      out += "<img src=\"";
      out += escape_html(n.url);
      out += "\" alt=\"";
      out += escape_html(collect_text(n.children));
      out += "\"";
      if (!n.title.empty()) {
        out += " title=\"";
        out += escape_html(n.title);
        out += "\"";
      }
      out += " />";
      break;
    }
    }
  }
  return out;
}

} // namespace

std::string render_inlines_html(
    std::string_view text,
    const std::unordered_map<std::string, LinkDef> *link_defs) {
  std::vector<InlineNode> nodes = parse_inlines(text, true, link_defs);
  return render_nodes_html(nodes);
}

std::string resolve_escapes_and_entities(std::string_view text) {
  return normalize_title(text);
}
