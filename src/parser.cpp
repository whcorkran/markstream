#include "parser.hpp"
#include "ast_node.hpp"
#include "scanners.hpp"
#include <algorithm>
#include <cctype>

#define CODE_INDENT 4
#define TAB_STOP 4

Parser::Parser() : current_line_(0) {
  root_ = ASTNode::create(NodeType::Document, 1, 1);
  root_->set_open(true);
  open_blocks_.push_back(root_);
}

// get the deepest open block
ASTNode::Ptr Parser::get_deepest_open_block() const {
  return open_blocks_.back();
}

// check if document has no open nested blocks
bool Parser::is_complete() const { return open_blocks_.size() <= 1; }

void Parser::finish_document() {
  finalize_above(1);
  finalize(root_);
  open_blocks_.clear();
  open_blocks_.push_back(root_);
}

void Parser::reset() {
  root_ = ASTNode::create(NodeType::Document, 1, 1);
  root_->set_open(true);
  open_blocks_.clear();
  open_blocks_.push_back(root_);
  current_line_ = 0;
  pre_phase2_depth_ = 0;
  link_defs_.clear();
}

// line processing helpers
char Parser::peek_at(const std::string &input, size_t pos) const {
  if (pos >= input.size())
    return '\0';
  return input[pos];
}

Parser::FirstNonspace Parser::find_first_nonspace(const std::string &line,
                                                  size_t offset,
                                                  size_t column) const {
  FirstNonspace result;
  result.offset = offset;
  result.column = column;

  int chars_to_tab = TAB_STOP - (column % TAB_STOP);

  while (result.offset < line.size()) {
    char c = line[result.offset];
    if (c == ' ') {
      result.offset += 1;
      result.column += 1;
      chars_to_tab -= 1;
      if (chars_to_tab == 0) {
        chars_to_tab = TAB_STOP;
      }
    } else if (c == '\t') {
      result.offset += 1;
      result.column += chars_to_tab;
      chars_to_tab = TAB_STOP;
    } else {
      break;
    }
  }

  result.indent = static_cast<int>(result.column - column);
  result.blank = scan::is_line_end(peek_at(line, result.offset));
  return result;
}

void Parser::advance_offset(const std::string &line, size_t &offset,
                            size_t &column, size_t count, bool columns,
                            bool &partially_consumed_tab) const {
  while (count > 0 && offset < line.size()) {
    char c = line[offset];
    if (c == '\t') {
      size_t chars_to_tab = TAB_STOP - (column % TAB_STOP);
      if (columns) {
        partially_consumed_tab = chars_to_tab > count;
        size_t chars_to_advance = std::min(count, chars_to_tab);
        column += chars_to_advance;
        offset += (partially_consumed_tab ? 0 : 1);
        count -= chars_to_advance;
      } else {
        partially_consumed_tab = false;
        column += chars_to_tab;
        offset += 1;
        count -= 1;
      }
    } else {
      partially_consumed_tab = false;
      offset += 1;
      column += 1;
      count -= 1;
    }
  }
}

// ============================================================================
// Block type checks for later logic
// ============================================================================

bool Parser::can_contain(NodeType parent_type, NodeType child_type) const {
  return (parent_type == NodeType::Document ||
          parent_type == NodeType::BlockQuote ||
          parent_type == NodeType::Item ||
          (parent_type == NodeType::List && child_type == NodeType::Item));
}

bool Parser::accepts_lines(NodeType block_type) const {
  return (
      block_type == NodeType::Paragraph || block_type == NodeType::Heading ||
      block_type == NodeType::CodeBlock || block_type == NodeType::HtmlBlock);
}

bool Parser::last_child_is_open(ASTNode::Ptr container) const {
  if (!container)
    return false;
  ASTNode::Ptr last = container->last_child();
  return last && last->is_open();
}

// ============================================================================
// Block creation and finalization using open_blocks_ stack
// ============================================================================

ASTNode::Ptr Parser::add_child(ASTNode::Ptr parent, NodeType block_type,
                               int start_column) {
  if (!parent)
    return nullptr;

  // First, finalize any blocks on the stack that are deeper than parent.
  // These are unmatched blocks from phase 1 that must be closed before
  // we create a new block.  This ensures that e.g. a thematic break at
  // the document level isn't placed inside an unmatched list item.
  for (size_t i = 0; i < open_blocks_.size(); i++) {
    if (open_blocks_[i] == parent) {
      while (open_blocks_.size() > i + 1) {
        finalize(open_blocks_.back());
        open_blocks_.pop_back();
      }
      break;
    }
  }

  // Now walk up from parent if it can't contain this child type
  while (!can_contain(parent->type(), block_type)) {
    finalize(open_blocks_.back());
    open_blocks_.pop_back();
    parent = open_blocks_.back();
  }

  ASTNode::Ptr child = ASTNode::create(block_type, current_line_, start_column);
  parent->add_child(child);
  open_blocks_.push_back(child);
  return child;
}

// ============================================================================
// Link reference definition parsing
// ============================================================================

// Decode one UTF-8 codepoint starting at s[pos].
// Returns {codepoint, byte_count}. On invalid UTF-8, returns {byte, 1}.
static std::pair<uint32_t, size_t> decode_utf8(std::string_view s, size_t pos) {
  unsigned char c = static_cast<unsigned char>(s[pos]);
  if (c < 0x80) return {c, 1};
  if ((c & 0xE0) == 0xC0 && pos + 1 < s.size()) {
    uint32_t cp = (c & 0x1F) << 6 | (static_cast<unsigned char>(s[pos+1]) & 0x3F);
    return {cp, 2};
  }
  if ((c & 0xF0) == 0xE0 && pos + 2 < s.size()) {
    uint32_t cp = (c & 0x0F) << 12 |
        (static_cast<unsigned char>(s[pos+1]) & 0x3F) << 6 |
        (static_cast<unsigned char>(s[pos+2]) & 0x3F);
    return {cp, 3};
  }
  if ((c & 0xF8) == 0xF0 && pos + 3 < s.size()) {
    uint32_t cp = (c & 0x07) << 18 |
        (static_cast<unsigned char>(s[pos+1]) & 0x3F) << 12 |
        (static_cast<unsigned char>(s[pos+2]) & 0x3F) << 6 |
        (static_cast<unsigned char>(s[pos+3]) & 0x3F);
    return {cp, 4};
  }
  return {c, 1};
}

// Simple Unicode case folding for common ranges.
// Returns the lowercase codepoint.
static uint32_t unicode_tolower(uint32_t cp) {
  // ASCII
  if (cp >= 'A' && cp <= 'Z') return cp + 32;
  // Latin-1 Supplement (À-Ö, Ø-Þ)
  if (cp >= 0xC0 && cp <= 0xD6) return cp + 32;
  if (cp >= 0xD8 && cp <= 0xDE) return cp + 32;
  // Greek (Α-Ρ, Σ-Ω)
  if (cp >= 0x391 && cp <= 0x3A1) return cp + 32;
  if (cp >= 0x3A3 && cp <= 0x3A9) return cp + 32;
  // Cyrillic (А-Я)
  if (cp >= 0x410 && cp <= 0x42F) return cp + 32;
  return cp;
}

// Encode a codepoint as UTF-8
static std::string encode_utf8_cp(uint32_t cp) {
  std::string out;
  if (cp <= 0x7F) {
    out += static_cast<char>(cp);
  } else if (cp <= 0x7FF) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
  return out;
}

static std::string normalize_label(std::string_view label) {
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
      auto [cp, bytes] = decode_utf8(label, i);
      uint32_t lower = unicode_tolower(cp);
      result += encode_utf8_cp(lower);
      in_space = false;
      i += bytes;
    }
  }
  return result;
}

// Try to parse a link reference definition starting at position `start`
// in `content`. Returns the number of characters consumed (0 if failed).
size_t Parser::try_parse_link_ref_def(const std::string &content, size_t start) {
  size_t i = start;
  size_t len = content.size();

  // Skip up to 3 spaces of indentation
  size_t indent = 0;
  while (i < len && content[i] == ' ' && indent < 3) {
    i++;
    indent++;
  }

  // Must start with [
  if (i >= len || content[i] != '[')
    return 0;
  i++;

  // Parse label (up to 999 chars, no empty, no unescaped [)
  size_t label_start = i;
  int label_len = 0;
  bool found_close = false;
  while (i < len && label_len < 1000) {
    if (content[i] == '\\' && i + 1 < len) {
      i += 2;
      label_len += 2;
      continue;
    }
    if (content[i] == '[')
      return 0;
    if (content[i] == ']') {
      found_close = true;
      break;
    }
    i++;
    label_len++;
  }
  if (!found_close)
    return 0;
  std::string_view label_raw = std::string_view(content).substr(
      label_start, i - label_start);
  std::string label = normalize_label(label_raw);
  if (label.empty())
    return 0;
  i++; // skip ]

  // Must be followed by :
  if (i >= len || content[i] != ':')
    return 0;
  i++; // skip :

  // Optional whitespace (including at most one newline)
  bool had_newline = false;
  while (i < len && (content[i] == ' ' || content[i] == '\t')) i++;
  if (i < len && content[i] == '\n') {
    i++;
    had_newline = true;
    while (i < len && (content[i] == ' ' || content[i] == '\t')) i++;
  }

  // Parse destination
  std::string url;
  if (i >= len || content[i] == '\n')
    return 0;

  if (content[i] == '<') {
    // Angle-bracket destination
    size_t dest_start = i + 1;
    i++;
    while (i < len) {
      if (content[i] == '\\' && i + 1 < len && content[i + 1] != '\n') {
        i += 2;
        continue;
      }
      if (content[i] == '>') {
        url = content.substr(dest_start, i - dest_start);
        i++;
        break;
      }
      if (content[i] == '<' || content[i] == '\n')
        return 0;
      i++;
    }
    if (url.empty() && (i >= len || content[i - 1] != '>'))
      return 0; // unclosed <
    // Handle empty angle-bracket <> case
    if (i > 0 && content[i - 1] == '>' && dest_start == i - 1) {
      url = "";
    }
  } else {
    // Bare destination
    size_t dest_start = i;
    int paren_depth = 0;
    while (i < len) {
      char c = content[i];
      if (c == '\\' && i + 1 < len && content[i + 1] != '\n') {
        i += 2;
        continue;
      }
      if (c == ' ' || c == '\t' || c == '\n' || (c == ')' && paren_depth == 0))
        break;
      if (c == '(') paren_depth++;
      else if (c == ')') paren_depth--;
      i++;
    }
    if (i == dest_start)
      return 0;
    url = content.substr(dest_start, i - dest_start);
  }

  // Check for optional title
  std::string title;
  size_t before_title = i;

  // Skip whitespace before title
  bool title_had_newline = false;
  while (i < len && (content[i] == ' ' || content[i] == '\t')) i++;
  if (i < len && content[i] == '\n') {
    i++;
    title_had_newline = true;
    while (i < len && (content[i] == ' ' || content[i] == '\t')) i++;
  }

  bool has_title = false;
  bool had_ws_before_title = (i != before_title);
  if (had_ws_before_title && i < len &&
      (content[i] == '"' || content[i] == '\'' || content[i] == '(')) {
    char opener = content[i];
    char closer = (opener == '(') ? ')' : opener;
    size_t title_start = i + 1;
    i++;
    bool found_closer = false;
    while (i < len) {
      if (content[i] == '\\' && i + 1 < len) {
        i += 2;
        continue;
      }
      if (content[i] == closer) {
        title = content.substr(title_start, i - title_start);
        i++;
        has_title = true;
        found_closer = true;
        break;
      }
      if (content[i] == '\n' && opener != '(' && closer != ')') {
        // Titles can span lines
      }
      i++;
    }
    if (!found_closer) {
      // Title not closed, backtrack
      i = before_title;
      has_title = false;
    }
  } else {
    // No title — backtrack to before title whitespace
    i = before_title;
  }

  // After destination (and optional title), must be end of line or end of string
  // Skip trailing spaces
  while (i < len && (content[i] == ' ' || content[i] == '\t')) i++;
  if (i < len && content[i] != '\n') {
    // Not a valid def if there's non-whitespace remaining on the line
    // Try without title
    if (has_title) {
      i = before_title;
      has_title = false;
      title.clear();
      while (i < len && (content[i] == ' ' || content[i] == '\t')) i++;
      if (i < len && content[i] != '\n')
        return 0;
    } else {
      return 0;
    }
  }
  if (i < len && content[i] == '\n')
    i++;

  // Store definition (first one wins)
  if (link_defs_.find(label) == link_defs_.end()) {
    link_defs_[label] = LinkDef{std::move(url), std::move(title)};
  }

  return i - start;
}

// Walk down last-child chains of List/Item nodes to check whether the
// subtree effectively ends with a blank line.  Leaf nodes (Paragraph,
// CodeBlock, …) keep their last_line_blank flag intact because the
// propagation in add_text_to_container only clears ancestors.
static bool ends_with_blank_line(const ASTNode::Ptr &node) {
  ASTNode::Ptr n = node;
  while (n) {
    if (n->last_line_blank())
      return true;
    NodeType t = n->type();
    if (t == NodeType::List || t == NodeType::Item) {
      n = n->last_child();
    } else {
      return false;
    }
  }
  return false;
}

// Finalize a single block: close it, perform type-specific cleanup.
// The caller is responsible for popping from open_blocks_ when appropriate.
void Parser::finalize(ASTNode::Ptr b) {
  if (!b || !b->is_open()) {
    return;
  }

  b->set_open(false);

  // set end position to previous line (block ended before current line)
  b->set_end(current_line_ > 0 ? current_line_ - 1 : 0, 0);

  // Process content based on block type
  NodeType btype = b->type();

  switch (btype) {
  case NodeType::CodeBlock: {
    const CodeData *code = b->get_data<CodeData>();
    if (code && !code->is_fenced()) {
      // Indented code: remove trailing blank lines only (not trailing spaces
      // on the last content line)
      std::string &text = const_cast<std::string &>(b->content());
      // Find last non-blank line
      size_t end = text.size();
      while (end > 0) {
        // Find start of last line
        size_t line_start;
        if (end >= 2) {
          size_t pos = text.rfind('\n', end - 2);
          line_start = (pos == std::string::npos) ? 0 : pos + 1;
        } else {
          line_start = 0;
        }
        // Check if this line (from line_start to end-1, excluding the \n at
        // end-1) is blank
        bool blank = true;
        for (size_t k = line_start; k + 1 < end; k++) {
          if (text[k] != ' ' && text[k] != '\t') {
            blank = false;
            break;
          }
        }
        if (!blank)
          break;
        end = line_start;
      }
      if (end < text.size()) {
        text.resize(end);
      }
      if (text.empty() || text.back() != '\n')
        text += '\n';
    }
    break;
  }

  case NodeType::List: {
    // Determine tight/loose status by iterating children vectors.
    // Use ends_with_blank_line() which walks last-child chains to find
    // blank-line flags that survive the ancestor-clearing propagation.
    ListData *list_data = b->get_data<ListData>();
    if (list_data) {
      list_data->is_tight = true;

      const auto &items = b->children();
      for (size_t i = 0; i < items.size(); i++) {
        const auto &item = items[i];
        bool has_next_item = (i + 1 < items.size());

        if (ends_with_blank_line(item) && has_next_item) {
          list_data->is_tight = false;
          break;
        }

        // Check children of list item
        const auto &subitems = item->children();
        for (size_t j = 0; j < subitems.size(); j++) {
          const auto &subitem = subitems[j];
          bool has_next_subitem = (j + 1 < subitems.size());
          if ((has_next_item || has_next_subitem) &&
              ends_with_blank_line(subitem)) {
            list_data->is_tight = false;
            break;
          }
        }
        if (!list_data->is_tight)
          break;
      }
    }
    break;
  }

  case NodeType::Paragraph: {
    // Try to extract link reference definitions from the start
    std::string &text = const_cast<std::string &>(b->content());
    size_t pos = 0;
    while (pos < text.size()) {
      size_t consumed = try_parse_link_ref_def(text, pos);
      if (consumed == 0)
        break;
      pos += consumed;
    }
    if (pos > 0) {
      if (pos >= text.size()) {
        // Entire paragraph was link ref defs — mark content empty.
        // The node will be skipped during rendering (empty content).
        text.clear();
      } else {
        text = text.substr(pos);
      }
    }
    break;
  }

  default:
    break;
  }
}

// Finalize and pop all blocks on the stack above (and including) target_depth.
// After this call, open_blocks_.size() == target_depth.
void Parser::finalize_above(size_t target_depth) {
  while (open_blocks_.size() > target_depth) {
    finalize(open_blocks_.back());
    open_blocks_.pop_back();
  }
}

// ============================================================================
// Block continuation checkers
// ============================================================================

bool Parser::parse_block_quote_prefix(const std::string &line, size_t &offset,
                                      size_t &column,
                                      bool &partially_consumed_tab,
                                      const FirstNonspace &fn) const {
  if (fn.indent <= 3 && peek_at(line, fn.offset) == '>') {
    advance_offset(line, offset, column, fn.indent + 1, true,
                   partially_consumed_tab);

    if (scan::is_space_or_tab(peek_at(line, offset))) {
      advance_offset(line, offset, column, 1, true, partially_consumed_tab);
    }

    return true;
  }
  return false;
}

bool Parser::parse_list_item_prefix(const std::string &line,
                                    ASTNode::Ptr container, size_t &offset,
                                    size_t &column,
                                    bool &partially_consumed_tab,
                                    const FirstNonspace &fn) const {
  if (!container)
    return false;

  const ListData *list_data = container->get_data<ListData>();
  if (!list_data)
    return false;

  if (fn.indent >= list_data->marker_offset + list_data->padding) {
    advance_offset(line, offset, column,
                   list_data->marker_offset + list_data->padding, true,
                   partially_consumed_tab);
    return true;
  } else if (fn.blank && container->first_child()) {
    // Lazy continuation
    advance_offset(line, offset, column, fn.offset - offset, false,
                   partially_consumed_tab);
    return true;
  }
  return false;
}

bool Parser::parse_code_block_prefix(const std::string &line,
                                     ASTNode::Ptr container, size_t &offset,
                                     size_t &column,
                                     bool &partially_consumed_tab,
                                     bool *should_continue,
                                     const FirstNonspace &fn) const {
  if (!container)
    return false;

  const CodeData *code = container->get_data<CodeData>();
  if (!code)
    return false;

  *should_continue = true;

  if (!code->is_fenced()) {
    // indented code
    if (fn.indent >= CODE_INDENT) {
      advance_offset(line, offset, column, CODE_INDENT, true,
                     partially_consumed_tab);
      return true;
    } else if (fn.blank) {
      advance_offset(line, offset, column, fn.offset - offset, false,
                     partially_consumed_tab);
      return true;
    }
  } else {
    // fenced code
    size_t matched = 0;
    if (fn.indent <= 3 && peek_at(line, fn.offset) == code->fence_char) {
      matched = scan_close_code_fence(line, fn.offset, code->fence_char,
                                      code->fence_length);
    }

    if (matched >= code->fence_length) {
      // closing fence
      *should_continue = false;
      advance_offset(line, offset, column, matched, false,
                     partially_consumed_tab);
      return true;
    } else {
      // skip optional spaces of fence offset
      int i = code->fence_offset;
      while (i > 0 && scan::is_space_or_tab(peek_at(line, offset))) {
        advance_offset(line, offset, column, 1, true, partially_consumed_tab);
        i--;
      }
      return true;
    }
  }
  return false;
}

bool Parser::parse_html_block_prefix(ASTNode::Ptr container,
                                     const FirstNonspace &fn) const {
  if (!container)
    return false;

  const int *html_type = container->get_data<int>();
  if (!html_type || *html_type < 1 || *html_type > 7)
    return false;

  switch (*html_type) {
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
    return true;
  case 6:
  case 7:
    return !fn.blank;
  default:
    return false;
  }
}

// ============================================================================
// Text accumulation
// ============================================================================

void Parser::add_line(ASTNode::Ptr target, const std::string &line,
                      size_t offset, size_t column,
                      bool partially_consumed_tab) {
  if (partially_consumed_tab) {
    offset += 1; // skip over tab
    size_t chars_to_tab = TAB_STOP - (column % TAB_STOP);
    constexpr char spaces[] = "   "; // max 3 spaces (TAB_STOP=4, min 1)
    target->append_content(std::string_view(spaces, chars_to_tab));
  }

  if (offset < line.size()) {
    target->append_content(std::string_view(line).substr(offset));
  }
}

void Parser::chop_trailing_hashtags(std::string &line) const {
  // Remove trailing newlines/spaces/tabs
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                           line.back() == '\n' || line.back() == '\r')) {
    line.pop_back();
  }

  size_t orig_n = line.size();
  size_t n = orig_n;

  // Remove trailing #
  while (n > 0 && line[n - 1] == '#') {
    n--;
  }

  // Check for space before the final # (or entire content is #)
  if (n != orig_n && (n == 0 || scan::is_space_or_tab(line[n - 1]))) {
    if (n == 0) {
      line.clear();
    } else {
      line.resize(n - 1);
      // Remove trailing spaces again
      while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
      }
    }
  }

  // Add back the newline
  line += '\n';
}

// ============================================================================
// Core algorithm - Phase 1: Check open blocks
// ============================================================================

ASTNode::Ptr Parser::check_open_blocks(const std::string &line,
                                       bool *all_matched, size_t &offset,
                                       size_t &column,
                                       bool &partially_consumed_tab) {
  bool should_continue = true;
  *all_matched = false;

  // Walk open_blocks_ from index 1 (skip root) to find deepest matched.
  // matched_depth tracks how far we successfully matched.
  size_t matched_depth = 0; // index into open_blocks_ of last matched

  for (size_t i = 1; i < open_blocks_.size(); i++) {
    ASTNode::Ptr container = open_blocks_[i];
    NodeType cont_type = container->type();

    FirstNonspace fn = find_first_nonspace(line, offset, column);

    switch (cont_type) {
    case NodeType::BlockQuote:
      if (!parse_block_quote_prefix(line, offset, column,
                                    partially_consumed_tab, fn))
        goto done;
      break;

    case NodeType::List:
      // lists don't have special continuation - items do
      break;

    case NodeType::Item:
      if (!parse_list_item_prefix(line, container, offset, column,
                                  partially_consumed_tab, fn))
        goto done;
      break;

    case NodeType::CodeBlock:
      if (!parse_code_block_prefix(line, container, offset, column,
                                   partially_consumed_tab, &should_continue,
                                   fn))
        goto done;
      if (!should_continue) {
        // Closing fence found: finalize the code block and everything above
        finalize(container);
        open_blocks_.erase(open_blocks_.begin() + static_cast<long>(i));
        return nullptr; // null signals stop
      }
      break;

    case NodeType::Heading:
      // heading can never contain more than one line
      goto done;

    case NodeType::HtmlBlock:
      if (!parse_html_block_prefix(container, fn))
        goto done;
      break;

    case NodeType::Paragraph:
      if (fn.blank)
        goto done;
      break;

    default:
      break;
    }

    matched_depth = i;
  }

  *all_matched = true;

done:
  if (!should_continue) {
    return nullptr;
  }

  // Return the last matched container
  // If all matched, return the deepest; otherwise return the parent of
  // the first unmatched block
  return open_blocks_[matched_depth];
}

// ============================================================================
// Core algorithm - Phase 2: New block starters
// ============================================================================

Parser::BlockStart Parser::try_block_quote(OpenBlockCtx &ctx) {
  if (ctx.indented || peek_at(ctx.line, ctx.fn.offset) != '>')
    return BlockStart::None;

  size_t startpos = ctx.fn.offset;
  advance_offset(ctx.line, ctx.offset, ctx.column,
                 ctx.fn.offset + 1 - ctx.offset, false,
                 ctx.partially_consumed_tab);
  if (scan::is_space_or_tab(peek_at(ctx.line, ctx.offset))) {
    advance_offset(ctx.line, ctx.offset, ctx.column, 1, true,
                   ctx.partially_consumed_tab);
  }
  ctx.container = add_child(ctx.container, NodeType::BlockQuote,
                            static_cast<int>(startpos + 1));
  return BlockStart::Found;
}

Parser::BlockStart Parser::try_atx_heading(OpenBlockCtx &ctx) {
  if (ctx.indented)
    return BlockStart::None;

  size_t matched = scan_atx_heading_start(ctx.line, ctx.fn.offset);
  if (!matched)
    return BlockStart::None;

  size_t startpos = ctx.fn.offset;
  advance_offset(ctx.line, ctx.offset, ctx.column,
                 ctx.fn.offset + matched - ctx.offset, false,
                 ctx.partially_consumed_tab);
  ctx.container = add_child(ctx.container, NodeType::Heading,
                            static_cast<int>(startpos + 1));

  HeadingData hdata{};
  hdata.level = static_cast<uint8_t>(matched);
  hdata.setext = false;
  ctx.container->set_data(hdata);
  return BlockStart::Leaf;
}

Parser::BlockStart Parser::try_code_fence(OpenBlockCtx &ctx) {
  if (ctx.indented)
    return BlockStart::None;

  CodeFenceInfo fence_info{};
  size_t matched = scan_open_code_fence(ctx.line, ctx.fn.offset, &fence_info);
  if (!matched)
    return BlockStart::None;

  ctx.container = add_child(ctx.container, NodeType::CodeBlock,
                            static_cast<int>(ctx.fn.offset + 1));

  CodeData cdata{};
  cdata.fence_char = fence_info.fence_char;
  cdata.fence_length =
      static_cast<uint8_t>(std::min(fence_info.fence_length, size_t(255)));
  cdata.fence_offset = static_cast<uint8_t>(ctx.fn.offset - ctx.offset);
  cdata.info = fence_info.info;
  ctx.container->set_data(cdata);

  // Advance past the entire opening fence line (info string is metadata, not
  // content)
  advance_offset(ctx.line, ctx.offset, ctx.column, ctx.line.size() - ctx.offset,
                 false, ctx.partially_consumed_tab);
  return BlockStart::Leaf;
}

Parser::BlockStart Parser::try_html_block(OpenBlockCtx &ctx) {
  if (ctx.indented)
    return BlockStart::None;

  HtmlBlockType html_type = scan_html_block_start(ctx.line, ctx.fn.offset);
  if (html_type == HtmlBlockType::None)
    return BlockStart::None;

  // Type 7 can't interrupt a paragraph
  if (html_type == HtmlBlockType::Type7 && ctx.maybe_lazy)
    return BlockStart::None;

  ctx.container = add_child(ctx.container, NodeType::HtmlBlock,
                            static_cast<int>(ctx.fn.offset + 1));
  ctx.container->set_data(static_cast<int>(html_type));
  return BlockStart::Leaf;
}

Parser::BlockStart Parser::try_setext_heading(OpenBlockCtx &ctx) {
  if (ctx.indented)
    return BlockStart::None;

  NodeType cont_type = ctx.container->type();
  if (cont_type != NodeType::Paragraph)
    return BlockStart::None;

  char setext_char;
  size_t matched =
      scan_setext_heading_line(ctx.line, ctx.fn.offset, &setext_char);
  if (!matched)
    return BlockStart::None;

  // Convert paragraph to setext heading.
  // The paragraph is at open_blocks_.back(). Its parent is one level up.
  int level = (setext_char == '=') ? 1 : 2;

  // Extract link reference definitions from paragraph content first
  std::string para_content =
      std::move(const_cast<std::string &>(ctx.container->content()));
  size_t refdef_pos = 0;
  while (refdef_pos < para_content.size()) {
    size_t consumed = try_parse_link_ref_def(para_content, refdef_pos);
    if (consumed == 0)
      break;
    refdef_pos += consumed;
  }
  if (refdef_pos > 0) {
    para_content = para_content.substr(refdef_pos);
  }

  // If all content was link ref defs, the === line is not a setext heading
  if (para_content.empty()) {
    // Restore state: put remaining line as new paragraph content
    ctx.container->set_content(std::string(""));
    return BlockStart::None;
  }

  int start_line = ctx.container->start_line();
  int start_col = ctx.container->start_col();

  // Pop the paragraph off the open blocks stack
  open_blocks_.pop_back();
  ASTNode::Ptr parent = open_blocks_.back();

  // Create heading that replaces the paragraph in parent's children
  ASTNode::Ptr heading =
      ASTNode::create(NodeType::Heading, start_line, start_col);
  heading->set_content(std::move(para_content));

  HeadingData hdata{};
  hdata.level = static_cast<uint8_t>(level);
  hdata.setext = true;
  heading->set_data(hdata);

  // Replace the last child (paragraph) with the new heading
  parent->replace_last_child(heading);
  open_blocks_.push_back(heading);

  ctx.container = heading;
  advance_offset(ctx.line, ctx.offset, ctx.column,
                 ctx.line.size() - 1 - ctx.offset, false,
                 ctx.partially_consumed_tab);
  return BlockStart::Leaf;
}

Parser::BlockStart Parser::try_thematic_break(OpenBlockCtx &ctx) {
  if (ctx.indented)
    return BlockStart::None;

  // Thematic break cannot interrupt an unmatched paragraph
  NodeType cont_type = ctx.container->type();
  if (cont_type == NodeType::Paragraph && !ctx.all_matched)
    return BlockStart::None;

  char thematic_char;
  size_t matched = scan_thematic_break(ctx.line, ctx.fn.offset, &thematic_char);
  if (!matched)
    return BlockStart::None;

  ctx.container = add_child(ctx.container, NodeType::ThematicBreak,
                            static_cast<int>(ctx.fn.offset + 1));
  advance_offset(ctx.line, ctx.offset, ctx.column,
                 ctx.line.size() - 1 - ctx.offset, false,
                 ctx.partially_consumed_tab);
  return BlockStart::Leaf;
}

Parser::BlockStart Parser::try_list_item(OpenBlockCtx &ctx) {
  if (ctx.fn.indent >= CODE_INDENT)
    return BlockStart::None;

  ListMarkerInfo list_info{};
  size_t matched = scan_list_marker(ctx.line, ctx.fn.offset, &list_info);
  if (!matched)
    return BlockStart::None;

  // Check if list marker can interrupt paragraph
  bool interrupts_paragraph = ctx.container->type() == NodeType::Paragraph;
  if (interrupts_paragraph) {
    // Ordered list starting != 1 can't interrupt paragraph
    if (list_info.is_ordered && list_info.start_number != 1)
      return BlockStart::None;
    // Empty list item can't interrupt paragraph
    if (scan_blank_line(ctx.line, ctx.fn.offset + matched))
      return BlockStart::None;
  }

  // Advance past the list marker using column-based advancement.
  // This correctly handles tabs by tracking partially consumed tabs,
  // which preserves remaining tab columns for the content.
  advance_offset(ctx.line, ctx.offset, ctx.column,
                 list_info.marker_width, true,
                 ctx.partially_consumed_tab);

  // Create list data
  ListData ldata{};
  ldata.marker_char = list_info.marker_char;
  ldata.is_ordered = list_info.is_ordered;
  ldata.start = list_info.start_number;
  ldata.marker_offset = ctx.fn.indent;
  ldata.padding = static_cast<int>(list_info.marker_width);
  ldata.is_tight = true;

  // Check if we need a new list or can continue existing
  NodeType cont_type = ctx.container->type();
  if (cont_type != NodeType::List) {
    ctx.container = add_child(ctx.container, NodeType::List,
                              static_cast<int>(ctx.fn.offset + 1));
    ctx.container->set_data(ldata);
  } else {
    const ListData *existing = ctx.container->get_data<ListData>();
    if (!existing || !ldata.matches(*existing)) {
      ctx.container = add_child(ctx.container, NodeType::List,
                                static_cast<int>(ctx.fn.offset + 1));
      ctx.container->set_data(ldata);
    }
  }

  // Add list item
  ctx.container = add_child(ctx.container, NodeType::Item,
                            static_cast<int>(ctx.fn.offset + 1));
  ctx.container->set_data(ldata);
  return BlockStart::Found;
}

Parser::BlockStart Parser::try_indented_code(OpenBlockCtx &ctx) {
  if (!ctx.indented || ctx.maybe_lazy || ctx.fn.blank)
    return BlockStart::None;

  advance_offset(ctx.line, ctx.offset, ctx.column, CODE_INDENT, true,
                 ctx.partially_consumed_tab);
  ctx.container = add_child(ctx.container, NodeType::CodeBlock,
                            static_cast<int>(ctx.offset + 1));

  CodeData cdata{};
  cdata.fence_length = 0;
  cdata.fence_char = 0;
  cdata.fence_offset = 0;
  ctx.container->set_data(cdata);
  return BlockStart::Leaf;
}

// ============================================================================
// Core algorithm - Phase 2: Open new blocks
// ============================================================================

void Parser::open_new_blocks(ASTNode::Ptr *container, const std::string &line,
                             bool all_matched, size_t &offset, size_t &column,
                             bool &partially_consumed_tab) {
  if (!*container)
    return;

  ASTNode::Ptr current_block = get_deepest_open_block();
  bool maybe_lazy =
      current_block && current_block->type() == NodeType::Paragraph;

  while ((*container)->type() != NodeType::CodeBlock &&
         (*container)->type() != NodeType::HtmlBlock) {
    FirstNonspace fn = find_first_nonspace(line, offset, column);

    OpenBlockCtx ctx{*container,
                     line,
                     offset,
                     column,
                     partially_consumed_tab,
                     fn,
                     fn.indent >= CODE_INDENT,
                     maybe_lazy,
                     all_matched};

    // Try each block starter in priority order
    BlockStart result;
    if ((result = try_block_quote(ctx)) != BlockStart::None ||
        (result = try_atx_heading(ctx)) != BlockStart::None ||
        (result = try_code_fence(ctx)) != BlockStart::None ||
        (result = try_html_block(ctx)) != BlockStart::None ||
        (result = try_setext_heading(ctx)) != BlockStart::None ||
        (result = try_thematic_break(ctx)) != BlockStart::None ||
        (result = try_list_item(ctx)) != BlockStart::None ||
        (result = try_indented_code(ctx)) != BlockStart::None) {
      // Leaf blocks accept lines -- done opening blocks
      if (result == BlockStart::Leaf || accepts_lines((*container)->type()))
        break;
      // Found a container block -- loop to check for nested blocks
      maybe_lazy = false;
      continue;
    }

    // Nothing matched
    break;
  }
}

// ============================================================================
// Core algorithm - Phase 3: Add text to container
// ============================================================================

void Parser::add_text_to_container(ASTNode::Ptr container,
                                   ASTNode::Ptr last_matched_container,
                                   ASTNode::Ptr deepest_before_new,
                                   const std::string &line, size_t &offset,
                                   size_t &column, bool &partially_consumed_tab,
                                   const FirstNonspace &fn) {
  if (fn.blank && container->last_child()) {
    container->last_child()->set_last_line_blank(true);
  }

  NodeType cont_type = container->type();
  const CodeData *code = container->get_data<CodeData>();

  bool is_blank_allowed =
      (cont_type != NodeType::BlockQuote && cont_type != NodeType::Heading &&
       cont_type != NodeType::ThematicBreak &&
       !(cont_type == NodeType::CodeBlock && code && code->is_fenced()) &&
       !(cont_type == NodeType::Item && !container->first_child() &&
         container->start_line() == current_line_));

  container->set_last_line_blank(fn.blank && is_blank_allowed);

  // Propagate: ancestor containers (above container on the stack) are not
  // blank on this line.  Only walk upward — do NOT clear descendants that
  // are deeper on the stack, matching cmark's parent-walk behaviour.
  for (auto &ob : open_blocks_) {
    if (ob == container)
      break;
    ob->set_last_line_blank(false);
  }

  // Lazy continuation check: if the deepest open block (from before phase 2)
  // was a paragraph that wasn't matched, and no new blocks were opened,
  // the line lazily continues the paragraph.
  if (deepest_before_new != last_matched_container &&
      container == last_matched_container && !fn.blank && deepest_before_new &&
      deepest_before_new->type() == NodeType::Paragraph) {
    advance_offset(line, offset, column, fn.offset - offset, false,
                   partially_consumed_tab);
    add_line(deepest_before_new, line, offset, column, partially_consumed_tab);
  } else {
    // Finalize remaining unmatched blocks.  When phase 2 opened new blocks,
    // add_child() already finalized unmatched blocks above the parent.
    // We only need to finalize here when phase 2 did NOT open anything
    // (container == last_matched_container).
    if (container == last_matched_container) {
      bool found = false;
      size_t matched_idx = 0;
      for (size_t i = 0; i < open_blocks_.size(); i++) {
        if (open_blocks_[i] == last_matched_container) {
          matched_idx = i;
          found = true;
          break;
        }
      }

      if (found) {
        size_t finalize_end = std::min(pre_phase2_depth_, open_blocks_.size());
        if (finalize_end > matched_idx + 1) {
          for (size_t i = finalize_end; i > matched_idx + 1; i--) {
            finalize(open_blocks_[i - 1]);
          }
          open_blocks_.erase(
              open_blocks_.begin() + static_cast<long>(matched_idx + 1),
              open_blocks_.begin() + static_cast<long>(finalize_end));
        }
      }
    }

    NodeType container_type = container->type();
    if (container_type == NodeType::CodeBlock) {
      add_line(container, line, offset, column, partially_consumed_tab);
    } else if (container_type == NodeType::HtmlBlock) {
      add_line(container, line, offset, column, partially_consumed_tab);

      // Check for HTML block end
      const int *html_type = container->get_data<int>();
      if (html_type) {
        if (scan_html_block_end(line, fn.offset,
                                static_cast<HtmlBlockType>(*html_type))) {
          finalize(container);
          // Find and remove container from open_blocks_
          for (auto it = open_blocks_.begin(); it != open_blocks_.end(); ++it) {
            if (*it == container) {
              open_blocks_.erase(it);
              break;
            }
          }
        }
      }
    } else if (fn.blank) {
      // Do nothing for blank lines
    } else if (accepts_lines(container_type)) {
      const HeadingData *heading = container->get_data<HeadingData>();
      if (container_type == NodeType::Heading && heading && !heading->setext) {
        // ATX heading - chop trailing hashtags in-place (line is line_buffer_)
        chop_trailing_hashtags(line_buffer_);
        add_line(container, line_buffer_, fn.offset, fn.column, false);
      } else {
        advance_offset(line, offset, column, fn.offset - offset, false,
                       partially_consumed_tab);
        add_line(container, line, offset, column, partially_consumed_tab);
      }
    } else {
      // Create paragraph container
      container = add_child(container, NodeType::Paragraph,
                            static_cast<int>(fn.offset + 1));
      advance_offset(line, offset, column, fn.offset - offset, false,
                     partially_consumed_tab);
      add_line(container, line, offset, column, partially_consumed_tab);
    }
  }
}

// ============================================================================
// Main entry point
// ============================================================================

void Parser::parse_line(std::string_view line) {
  line_buffer_.assign(line);

  // Ensure line ends with newline
  if (line_buffer_.empty() || !scan::is_line_end(line_buffer_.back())) {
    line_buffer_ += '\n';
  }

  current_line_++;

  // Initialize parser state
  size_t offset = 0;
  size_t column = 0;
  bool partially_consumed_tab = false;

  bool all_matched = true;
  ASTNode::Ptr last_matched_container = check_open_blocks(
      line_buffer_, &all_matched, offset, column, partially_consumed_tab);

  if (last_matched_container) {
    // Save the deepest open block and stack depth before phase 2 creates
    // new blocks. Phase 3 needs this to finalize unmatched blocks correctly
    // without touching newly created blocks.
    ASTNode::Ptr deepest_before_new = get_deepest_open_block();
    pre_phase2_depth_ = open_blocks_.size();

    ASTNode::Ptr container = last_matched_container;
    open_new_blocks(&container, line_buffer_, all_matched, offset, column,
                    partially_consumed_tab);

    FirstNonspace fn = find_first_nonspace(line_buffer_, offset, column);
    add_text_to_container(container, last_matched_container, deepest_before_new,
                          line_buffer_, offset, column, partially_consumed_tab,
                          fn);
  }
}
