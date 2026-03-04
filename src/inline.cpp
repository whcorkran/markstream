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
      // Special case: ẞ (U+1E9E) case-folds to "ss"
      if (cp == 0x1E9E) {
        result += "ss";
      } else {
        uint32_t lower = inline_unicode_tolower(cp);
        result += encode_utf8(lower);
      }
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
  // Scheme must be 2-32 characters
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
  // URI part: no spaces, <, > characters
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
  // Local part: alphanumeric or .!#$%&'*+/=?^_`{|}~-
  for (size_t i = 0; i < at; i++) {
    char c = s[i];
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
          c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
          c == '\'' || c == '*' || c == '+' || c == '/' || c == '=' ||
          c == '?' || c == '^' || c == '_' || c == '`' || c == '{' ||
          c == '|' || c == '}' || c == '~' || c == '-'))
      return false;
  }
  // Domain: label segments separated by dots
  // Each label: [a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?
  bool has_dot = false;
  size_t seg_start = at + 1;
  for (size_t i = at + 1; i <= s.size(); i++) {
    if (i == s.size() || s[i] == '.') {
      size_t seg_len = i - seg_start;
      if (seg_len == 0 || seg_len > 63)
        return false;
      if (!std::isalnum(static_cast<unsigned char>(s[seg_start])))
        return false;
      if (!std::isalnum(static_cast<unsigned char>(s[i - 1])))
        return false;
      for (size_t j = seg_start; j < i; j++) {
        if (!(std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '-'))
          return false;
      }
      if (i < s.size()) {
        has_dot = true;
        seg_start = i + 1;
      }
    }
  }
  return has_dot;
}

// Spec-compliant inline HTML scanner.
// Returns position after closing '>' if valid inline HTML, else npos.
// Handles: open tags, close tags, comments, PI, declarations, CDATA.
size_t scan_inline_html(std::string_view s, size_t start) {
  size_t len = s.size();
  if (start + 1 >= len || s[start] != '<')
    return std::string_view::npos;

  char c1 = s[start + 1];

  // --- Open tag: <tagname (attr)* spaces? /? > ---
  if (std::isalpha(static_cast<unsigned char>(c1))) {
    size_t i = start + 2;
    // tag name: [A-Za-z][A-Za-z0-9-]*
    while (i < len && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                       s[i] == '-'))
      i++;
    // attributes
    for (;;) {
      // skip whitespace (spaces, tabs, up to one newline)
      size_t ws_start = i;
      bool had_nl = false;
      while (i < len && (s[i] == ' ' || s[i] == '\t' ||
                         (!had_nl && (s[i] == '\n' || s[i] == '\r')))) {
        if (s[i] == '\n' || s[i] == '\r') {
          had_nl = true;
          if (s[i] == '\r' && i + 1 < len && s[i + 1] == '\n')
            i++;
        }
        i++;
      }
      if (i >= len)
        return std::string_view::npos;
      if (s[i] == '>')
        return i + 1;
      if (s[i] == '/' && i + 1 < len && s[i + 1] == '>')
        return i + 2;
      // Must have had whitespace to start an attribute
      if (i == ws_start)
        return std::string_view::npos;
      // attribute_name: [A-Za-z_:][A-Za-z0-9_.:-]*
      if (!(std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_' ||
            s[i] == ':'))
        return std::string_view::npos;
      i++;
      while (i < len && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                         s[i] == '_' || s[i] == '.' || s[i] == ':' ||
                         s[i] == '-'))
        i++;
      // optional attribute_value_spec
      size_t before_eq = i;
      // skip spaces before =
      while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                         s[i] == '\r'))
        i++;
      if (i < len && s[i] == '=') {
        i++; // skip =
        // skip spaces after =
        while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                           s[i] == '\r'))
          i++;
        if (i >= len)
          return std::string_view::npos;
        if (s[i] == '\'' || s[i] == '"') {
          char q = s[i];
          i++;
          while (i < len && s[i] != q)
            i++;
          if (i >= len)
            return std::string_view::npos;
          i++; // skip closing quote
        } else {
          // unquoted: [^"'=<>`\s]+
          size_t val_start = i;
          while (i < len && s[i] != '"' && s[i] != '\'' && s[i] != '=' &&
                 s[i] != '<' && s[i] != '>' && s[i] != '`' &&
                 !is_ascii_space(s[i]))
            i++;
          if (i == val_start)
            return std::string_view::npos;
        }
      } else {
        // No = sign — boolean attribute, backtrack whitespace
        i = before_eq;
      }
    }
  }

  // --- Closing tag: </tagname spaces? > ---
  if (c1 == '/' && start + 2 < len &&
      std::isalpha(static_cast<unsigned char>(s[start + 2]))) {
    size_t i = start + 3;
    while (i < len && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                       s[i] == '-'))
      i++;
    while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                       s[i] == '\r'))
      i++;
    if (i < len && s[i] == '>')
      return i + 1;
    return std::string_view::npos;
  }

  // --- HTML comment: <!-- ... --> ---
  // Scan for first --> after the opening <!--. The closing --> can
  // overlap with the opening (e.g. <!-->  and <!---> are valid).
  if (c1 == '!' && start + 3 < len && s[start + 2] == '-' &&
      s[start + 3] == '-') {
    for (size_t i = start + 2; i + 2 < len; i++) {
      if (s[i] == '-' && s[i + 1] == '-' && s[i + 2] == '>')
        return i + 3;
    }
    return std::string_view::npos;
  }

  // --- Processing instruction: <? ... ?> ---
  if (c1 == '?') {
    size_t i = start + 2;
    while (i + 1 < len) {
      if (s[i] == '?' && s[i + 1] == '>')
        return i + 2;
      i++;
    }
    return std::string_view::npos;
  }

  // --- CDATA: <![CDATA[ ... ]]> ---
  if (c1 == '!' && start + 8 < len && s[start + 2] == '[' &&
      s[start + 3] == 'C' && s[start + 4] == 'D' && s[start + 5] == 'A' &&
      s[start + 6] == 'T' && s[start + 7] == 'A' && s[start + 8] == '[') {
    size_t i = start + 9;
    while (i + 2 < len) {
      if (s[i] == ']' && s[i + 1] == ']' && s[i + 2] == '>')
        return i + 3;
      i++;
    }
    return std::string_view::npos;
  }

  // --- Declaration: <! UPPERCASE ... > ---
  if (c1 == '!' && start + 2 < len &&
      std::isupper(static_cast<unsigned char>(s[start + 2]))) {
    size_t i = start + 3;
    while (i < len && s[i] != '>')
      i++;
    if (i < len)
      return i + 1;
    return std::string_view::npos;
  }

  return std::string_view::npos;
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

// Unicode-aware punctuation check for CommonMark flanking rules.
// "Unicode punctuation character" = Pc, Pd, Pe, Pf, Pi, Po, Ps categories,
// OR anything in ASCII range that is not a letter, digit, or whitespace.
bool is_unicode_punct_cp(uint32_t cp) {
  // ASCII range: anything that's not letter, digit, or whitespace
  if (cp < 0x80) {
    return is_ascii_punct(static_cast<char>(cp));
  }
  // General punctuation block (U+2000-U+206F) — includes various dashes, quotes
  if (cp >= 0x2010 && cp <= 0x2027) return true; // Pd, Pi, Pf, Po, Ps, Pe
  if (cp >= 0x2030 && cp <= 0x205E) return true;
  // CJK punctuation
  if (cp >= 0x3000 && cp <= 0x303F) return true;
  // Fullwidth punctuation
  if (cp >= 0xFF01 && cp <= 0xFF0F) return true;
  if (cp >= 0xFF1A && cp <= 0xFF20) return true;
  if (cp >= 0xFF3B && cp <= 0xFF40) return true;
  if (cp >= 0xFF5B && cp <= 0xFF65) return true;
  // Latin-1 supplement punctuation
  if (cp == 0x00A1 || cp == 0x00A7 || cp == 0x00AB || cp == 0x00B6 ||
      cp == 0x00B7 || cp == 0x00BB || cp == 0x00BF) return true;
  // Specific Unicode punctuation categories
  if (cp == 0x2014 || cp == 0x2013) return true; // em dash, en dash
  if (cp >= 0x2018 && cp <= 0x201F) return true; // quotes
  if (cp == 0x2026) return true; // ellipsis
  // Sc (Symbol, currency) — included per CommonMark 0.31.2
  if (cp == 0x00A2 || cp == 0x00A3 || cp == 0x00A4 || cp == 0x00A5) return true;
  if (cp == 0x058F || cp == 0x060B || cp == 0x07FE || cp == 0x07FF) return true;
  if (cp == 0x09F2 || cp == 0x09F3 || cp == 0x09FB || cp == 0x0AF1) return true;
  if (cp == 0x0BF9 || cp == 0x0E3F || cp == 0x17DB) return true;
  if (cp >= 0x20A0 && cp <= 0x20CF) return true; // Currency symbols block (€ etc)
  if (cp == 0xA838 || cp == 0xFDFC || cp == 0xFE69 || cp == 0xFF04) return true;
  if (cp >= 0xFFE0 && cp <= 0xFFE6) return true;
  return false;
}

bool is_unicode_space_cp(uint32_t cp) {
  if (cp < 0x80) return is_ascii_space(static_cast<char>(cp));
  // Unicode Zs category (space separators)
  if (cp == 0x00A0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
      cp == 0x202F || cp == 0x205F || cp == 0x3000)
    return true;
  return false;
}

// Get character/codepoint before position (looking back through UTF-8)
uint32_t get_cp_before(std::string_view s, size_t pos) {
  if (pos == 0) return 0; // treat SOL as whitespace sentinel
  // Walk back to find start of UTF-8 sequence
  size_t back = pos - 1;
  while (back > 0 && (static_cast<unsigned char>(s[back]) & 0xC0) == 0x80)
    back--;
  auto [cp, len] = inline_decode_utf8(s, back);
  return cp;
}

uint32_t get_cp_at(std::string_view s, size_t pos) {
  if (pos >= s.size()) return 0; // treat EOL as whitespace sentinel
  auto [cp, len] = inline_decode_utf8(s, pos);
  return cp;
}

bool is_left_flanking_unicode(std::string_view s, size_t run_start, size_t run_end) {
  uint32_t before_cp = get_cp_before(s, run_start);
  uint32_t after_cp = get_cp_at(s, run_end);
  bool after_space = (after_cp == 0) || is_unicode_space_cp(after_cp);
  bool after_punct = (after_cp != 0) && is_unicode_punct_cp(after_cp);
  bool before_space = (before_cp == 0) || is_unicode_space_cp(before_cp);
  bool before_punct = (before_cp != 0) && is_unicode_punct_cp(before_cp);
  return !after_space && (!after_punct || before_space || before_punct);
}

bool is_right_flanking_unicode(std::string_view s, size_t run_start, size_t run_end) {
  uint32_t before_cp = get_cp_before(s, run_start);
  uint32_t after_cp = get_cp_at(s, run_end);
  bool after_space = (after_cp == 0) || is_unicode_space_cp(after_cp);
  bool after_punct = (after_cp != 0) && is_unicode_punct_cp(after_cp);
  bool before_space = (before_cp == 0) || is_unicode_space_cp(before_cp);
  bool before_punct = (before_cp != 0) && is_unicode_punct_cp(before_cp);
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

void process_emphasis(std::vector<InlineNode> *nodes, std::vector<Delimiter> *delims,
                      size_t start_delim = 0) {
  // openers_bottom prevents O(n²) behavior: when we fail to find an opener
  // for a closer of character c, we record how far back we searched so
  // future closers of the same character don't re-scan past that point.
  // Keyed by (delim_char * 4 + closer_length%3) to match the spec's
  // refined bottom tracking.
  std::unordered_map<int, ssize_t> openers_bottom;

  for (size_t ci = start_delim; ci < delims->size(); ci++) {
    bool keep_trying = true;
    while (keep_trying) {
      keep_trying = false;

      if (ci >= delims->size())
        break;
      Delimiter &closer = (*delims)[ci];
      if (!closer.active || !closer.can_close || closer.length == 0)
        break;

      int bottom_key = static_cast<int>(closer.delim_char) * 4 +
                        static_cast<int>(closer.length % 3);
      ssize_t bottom = static_cast<ssize_t>(start_delim) - 1;
      auto bit = openers_bottom.find(bottom_key);
      if (bit != openers_bottom.end())
        bottom = bit->second;

      ssize_t opener_i = -1;
      for (ssize_t oi = static_cast<ssize_t>(ci) - 1; oi > bottom; oi--) {
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

      if (opener_i < 0) {
        // No opener found — record bottom so we don't rescan this range
        openers_bottom[bottom_key] = static_cast<ssize_t>(ci) - 1;
        break;
      }

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
    std::string_view input,
    const std::unordered_map<std::string, LinkDef> *link_defs = nullptr);

size_t parse_link_label_end(std::string_view s, size_t open_bracket) {
  size_t depth = 0;
  // CommonMark spec: link labels can contain at most 999 characters.
  // Enforcing this limit also bounds each scan to O(999), preventing
  // O(n²) behavior when the input has many unclosed brackets.
  size_t label_chars = 0;
  for (size_t i = open_bracket; i < s.size(); i++) {
    char c = s[i];
    if (c == '\\') {
      if (i + 1 < s.size())
        i++;
      label_chars++;
      if (label_chars > 999)
        return std::string_view::npos;
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
      label_chars += (i - tick_start + 1);
      if (label_chars > 999)
        return std::string_view::npos;
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
    label_chars++;
    if (label_chars > 999)
      return std::string_view::npos;
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
    std::string_view input,
    const std::unordered_map<std::string, LinkDef> *link_defs) {
  struct BracketEntry {
    size_t node_index;   // index in nodes where [ or ![ text was inserted
    size_t delim_count;  // number of delimiters at time of push
    bool is_image;
    bool active;
    size_t input_pos;    // position in input AFTER the [ character
  };

  std::vector<InlineNode> nodes;
  std::vector<Delimiter> delimiters;
  std::vector<BracketEntry> brackets;

  size_t i = 0;
  while (i < input.size()) {
    char c = input[i];

    if (c == '*' || c == '_') {
      size_t run_start = i;
      while (i < input.size() && input[i] == c) {
        i++;
      }
      size_t run_len = i - run_start;

      bool left = is_left_flanking_unicode(input, run_start, i);
      bool right = is_right_flanking_unicode(input, run_start, i);
      bool can_open = left;
      bool can_close = right;
      if (c == '_') {
        uint32_t before_cp = get_cp_before(input, run_start);
        uint32_t after_cp = get_cp_at(input, i);
        can_open = left && (!right || is_unicode_punct_cp(before_cp));
        can_close = right && (!left || is_unicode_punct_cp(after_cp));
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
      // Try autolink first: find the first > that doesn't cross a newline
      size_t gt = input.find('>', i + 1);
      if (gt != std::string_view::npos) {
        std::string_view inner = input.substr(i + 1, gt - (i + 1));
        if (inner.find('\n') == std::string_view::npos &&
            inner.find('\r') == std::string_view::npos) {
          if (is_uri_autolink(inner) || is_email_autolink(inner)) {
            InlineNode link;
            link.kind = InlineKind::Link;
            std::string raw_url =
                is_email_autolink(inner)
                    ? ("mailto:" + std::string(inner))
                    : std::string(inner);
            std::string encoded_url;
            for (size_t ai = 0; ai < raw_url.size(); ai++) {
              unsigned char ac = static_cast<unsigned char>(raw_url[ai]);
              if (ac > 0x7F || ac == '"' || ac == '\\' || ac == '[' ||
                  ac == ']' || ac == '^' || ac == '`' || ac == '{' ||
                  ac == '|' || ac == '}' || ac < 0x20) {
                percent_encode_byte(encoded_url, ac);
              } else {
                encoded_url += static_cast<char>(ac);
              }
            }
            link.url = std::move(encoded_url);
            InlineNode txt;
            txt.kind = InlineKind::Text;
            txt.literal = std::string(inner);
            link.children.push_back(std::move(txt));
            nodes.push_back(std::move(link));
            i = gt + 1;
            continue;
          }
        }
      }

      // Try inline HTML (all 6 forms, possibly multi-line)
      size_t html_end = scan_inline_html(input, i);
      if (html_end != std::string_view::npos) {
        InlineNode html;
        html.kind = InlineKind::Html;
        html.literal = std::string(input.substr(i, html_end - i));
        nodes.push_back(std::move(html));
        i = html_end;
        continue;
      }

      append_text(&nodes, delimiters, "<");
      i++;
      continue;
    }

    // ![ — image bracket opener
    if (c == '!' && i + 1 < input.size() && input[i + 1] == '[') {
      InlineNode t;
      t.kind = InlineKind::Text;
      t.literal = "![";
      nodes.push_back(std::move(t));
      // Add dummy delimiter so append_text won't merge into this node
      delimiters.push_back({nodes.size() - 1, 0, '\0', false, false, true});
      brackets.push_back({nodes.size() - 1, delimiters.size(), true, true, i + 2});
      i += 2;
      continue;
    }

    // ! not followed by [
    if (c == '!') {
      append_text(&nodes, delimiters, "!");
      i++;
      continue;
    }

    // [ — link bracket opener
    if (c == '[') {
      InlineNode t;
      t.kind = InlineKind::Text;
      t.literal = "[";
      nodes.push_back(std::move(t));
      // Add dummy delimiter so append_text won't merge into this node
      delimiters.push_back({nodes.size() - 1, 0, '\0', false, false, true});
      brackets.push_back({nodes.size() - 1, delimiters.size(), false, true, i + 1});
      i++;
      continue;
    }

    // ] — try to close a bracket and form link/image
    if (c == ']') {
      // Find most recent bracket entry (active or not)
      if (brackets.empty()) {
        append_text(&nodes, delimiters, "]");
        i++;
        continue;
      }

      BracketEntry bracket = brackets.back();
      brackets.pop_back();

      if (!bracket.active) {
        // Inactive bracket — emit literal ]
        append_text(&nodes, delimiters, "]");
        i++;
        continue;
      }

      // Active bracket — try to form link/image
      std::string_view label = input.substr(bracket.input_pos,
                                            i - bracket.input_pos);
      size_t after_close = i + 1;
      std::string url, title;
      size_t end_pos = 0;
      bool matched = false;

      // Try inline link: ](url "title")
      if (parse_inline_link(input, after_close, &end_pos, &url, &title)) {
        matched = true;
      }

      // Try reference links
      if (!matched && link_defs) {
        if (after_close < input.size() && input[after_close] == '[') {
          // Could be collapsed [text][] or full reference [text][label]
          if (after_close + 1 < input.size() && input[after_close + 1] == ']') {
            // Collapsed reference: [text][]
            std::string key = normalize_label(label);
            auto it = link_defs->find(key);
            if (it != link_defs->end()) {
              url = it->second.url;
              title = it->second.title;
              end_pos = after_close + 2;
              matched = true;
            }
          }
          if (!matched) {
            // Full reference: [text][label]
            size_t ref_close = parse_link_label_end(input, after_close);
            if (ref_close != std::string_view::npos) {
              std::string_view ref_label =
                  input.substr(after_close + 1, ref_close - after_close - 1);
              std::string key = normalize_label(ref_label);
              if (!key.empty()) {
                auto it = link_defs->find(key);
                if (it != link_defs->end()) {
                  url = it->second.url;
                  title = it->second.title;
                  end_pos = ref_close + 1;
                  matched = true;
                }
              }
            }
          }
          // If [ follows ] but no reference matched, DON'T try shortcut
        } else {
          // No [ after ] — try shortcut reference: [text]
          std::string key = normalize_label(label);
          auto it = link_defs->find(key);
          if (it != link_defs->end()) {
            url = it->second.url;
            title = it->second.title;
            end_pos = after_close;
            matched = true;
          }
        }
      }

      if (!matched) {
        // Could not form link — emit literal ]
        append_text(&nodes, delimiters, "]");
        i++;
        continue;
      }

      // Successfully matched link/image!
      bool is_image = bracket.is_image;
      size_t opener_idx = bracket.node_index;

      // Process emphasis within the link/image range
      process_emphasis(&nodes, &delimiters, bracket.delim_count);

      // Collect children (all nodes after the opener text node)
      InlineNode link_node;
      link_node.kind = is_image ? InlineKind::Image : InlineKind::Link;
      link_node.url = normalize_url(url);
      link_node.title = normalize_title(title);

      size_t child_start = opener_idx + 1;
      for (size_t ni = child_start; ni < nodes.size(); ni++) {
        link_node.children.push_back(std::move(nodes[ni]));
      }

      // Remove children from main list and replace opener with link node
      nodes.erase(nodes.begin() + static_cast<long>(child_start), nodes.end());
      nodes[opener_idx] = std::move(link_node);

      // Deactivate delimiters that were within the link
      for (auto &d : delimiters) {
        if (d.node_index >= child_start) {
          d.active = false;
        }
      }

      // If this is a link (not image), deactivate all prior [ brackets
      // to prevent links within links
      if (!is_image) {
        for (auto &b : brackets) {
          if (!b.is_image) {
            b.active = false;
          }
        }
      }

      i = end_pos;
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
          ch == '<' || ch == '\n' || ch == '\r' || ch == '[' || ch == ']' ||
          ch == '!') {
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
  std::vector<InlineNode> nodes = parse_inlines(text, link_defs);
  return render_nodes_html(nodes);
}

std::string resolve_escapes_and_entities(std::string_view text) {
  return normalize_title(text);
}
