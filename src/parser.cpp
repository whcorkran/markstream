#include "parser.hpp"
#include "ast_node.hpp"
#include "scanners.hpp"
#include <algorithm>

#define CODE_INDENT 4
#define TAB_STOP 4

// ============================================================================
// Constructor
// ============================================================================

StreamParser::StreamParser() {
  root_ = ASTNode::create(NodeType::Document, 1, 1);
  root_->set_open(true);
  current_line_ = 0;
}

// ============================================================================
// Public methods
// ============================================================================

ASTNode::Ptr StreamParser::get_deepest_open_block() const {
  ASTNode::Ptr current = root_;
  while (current) {
    ASTNode::Ptr last = current->last_child();
    if (!last || !last->is_open()) {
      break;
    }
    current = last;
  }
  return current;
}

bool StreamParser::is_complete() const {
  if (!root_->is_open()) {
    return true;
  }
  ASTNode::Ptr deepest = get_deepest_open_block();
  return deepest == root_ && !deepest->is_open();
}

// ============================================================================
// Line processing helpers
// ============================================================================

char StreamParser::peek_at(const std::string &input, size_t pos) const {
  if (pos >= input.size())
    return '\0';
  return input[pos];
}

StreamParser::FirstNonspace
StreamParser::find_first_nonspace(const std::string &line, size_t offset,
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

void StreamParser::advance_offset(const std::string &line, size_t &offset,
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
// Block type checks
// ============================================================================

bool StreamParser::can_contain(NodeType parent_type,
                               NodeType child_type) const {
  return (parent_type == NodeType::Document ||
          parent_type == NodeType::BlockQuote ||
          parent_type == NodeType::Item ||
          (parent_type == NodeType::List && child_type == NodeType::Item));
}

bool StreamParser::accepts_lines(NodeType block_type) const {
  return (block_type == NodeType::Paragraph ||
          block_type == NodeType::Heading ||
          block_type == NodeType::CodeBlock ||
          block_type == NodeType::HtmlBlock);
}

bool StreamParser::last_child_is_open(ASTNode::Ptr container) const {
  if (!container)
    return false;
  ASTNode::Ptr last = container->last_child();
  return last && last->is_open();
}

// ============================================================================
// Block creation
// ============================================================================

ASTNode::Ptr StreamParser::add_child(ASTNode::Ptr parent, NodeType block_type,
                                     int start_column) {
  if (!parent)
    return nullptr;

  // If parent can't contain this child, finalize parent and move up
  std::string empty_line;
  while (!can_contain(parent->type(), block_type)) {
    parent = finalize(parent, 0, empty_line);
    if (!parent)
      return nullptr;
  }

  ASTNode::Ptr child = ASTNode::create(block_type, current_line_, start_column);
  parent->append_child(child);
  return child;
}

ASTNode::Ptr StreamParser::finalize(ASTNode::Ptr b, size_t last_line_length,
                                    const std::string &curline) {
  if (!b || !b->is_open()) {
    return b ? b->parent() : nullptr;
  }

  b->set_open(false);

  // Set end position
  if (curline.empty()) {
    // End of input
    b->set_end(current_line_, static_cast<int>(last_line_length));
  } else {
    NodeType btype = b->type();
    const CodeData *code = b->get_data<CodeData>();
    const HeadingData *heading = b->get_data<HeadingData>();

    if (btype == NodeType::Document ||
        (btype == NodeType::CodeBlock && code && code->is_fenced()) ||
        (btype == NodeType::Heading && heading && heading->setext)) {
      size_t end_col = curline.size();
      if (end_col > 0 && curline[end_col - 1] == '\n')
        end_col -= 1;
      if (end_col > 0 && curline[end_col - 1] == '\r')
        end_col -= 1;
      b->set_end(current_line_, static_cast<int>(end_col));
    } else {
      b->set_end(current_line_ - 1, static_cast<int>(last_line_length));
    }
  }

  // Process content based on block type
  NodeType btype = b->type();

  switch (btype) {
  case NodeType::Paragraph:
  case NodeType::Heading:
  case NodeType::HtmlBlock:
    // Store accumulated content
    if (!content_.empty()) {
      text_storage_[b.get()] = std::move(content_);
      content_.clear();
    }
    break;

  case NodeType::CodeBlock: {
    const CodeData *code = b->get_data<CodeData>();
    if (code && !code->is_fenced()) {
      // Indented code: remove trailing blank lines
      while (!content_.empty() &&
             (content_.back() == ' ' || content_.back() == '\t' ||
              content_.back() == '\n' || content_.back() == '\r')) {
        content_.pop_back();
      }
      content_ += '\n';
    }
    if (!content_.empty()) {
      text_storage_[b.get()] = std::move(content_);
      content_.clear();
    }
    break;
  }

  case NodeType::List: {
    // Determine tight/loose status
    ListData *list_data = b->get_data<ListData>();
    if (list_data) {
      list_data->is_tight = true;

      ASTNode::Ptr item = b->first_child();
      while (item) {
        if (item->last_line_blank() && item->next()) {
          list_data->is_tight = false;
          break;
        }

        // Check children of list item
        ASTNode::Ptr subitem = item->first_child();
        while (subitem) {
          if ((item->next() || subitem->next()) && subitem->last_line_blank()) {
            list_data->is_tight = false;
            break;
          }
          subitem = subitem->next();
        }
        if (!list_data->is_tight)
          break;
        item = item->next();
      }
    }
    break;
  }

  default:
    break;
  }

  return b->parent();
}

// ============================================================================
// Block continuation checkers
// ============================================================================

bool StreamParser::parse_block_quote_prefix(const std::string &line,
                                            size_t &offset, size_t &column,
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

bool StreamParser::parse_list_item_prefix(const std::string &line,
                                          ASTNode::Ptr container,
                                          size_t &offset, size_t &column,
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

bool StreamParser::parse_code_block_prefix(const std::string &line,
                                           ASTNode::Ptr container,
                                           size_t &offset, size_t &column,
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
    // Indented code
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
    // Fenced code
    size_t matched = 0;
    if (fn.indent <= 3 && peek_at(line, fn.offset) == code->fence_char) {
      matched = scan_close_code_fence(line, fn.offset, code->fence_char,
                                      code->fence_length);
    }

    if (matched >= code->fence_length) {
      // Closing fence
      *should_continue = false;
      advance_offset(line, offset, column, matched, false,
                     partially_consumed_tab);
      return true;
    } else {
      // Skip optional spaces of fence offset
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

bool StreamParser::parse_html_block_prefix(ASTNode::Ptr container,
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

void StreamParser::add_line(const std::string &line, size_t offset,
                            size_t column, bool partially_consumed_tab) {
  if (partially_consumed_tab) {
    offset += 1; // skip over tab
    // Add space characters
    size_t chars_to_tab = TAB_STOP - (column % TAB_STOP);
    for (size_t i = 0; i < chars_to_tab; i++) {
      content_ += ' ';
    }
  }

  if (offset < line.size()) {
    content_ += line.substr(offset);
  }
}

void StreamParser::chop_trailing_hashtags(std::string &line) const {
  // Remove trailing spaces
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }

  size_t orig_n = line.size();
  size_t n = orig_n;

  // Remove trailing #s
  while (n > 0 && line[n - 1] == '#') {
    n--;
  }

  // Check for space before the final #s
  if (n != orig_n && n > 0 && scan::is_space_or_tab(line[n - 1])) {
    line.erase(n - 1);
    // Remove trailing spaces again
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
  }
}

// ============================================================================
// Core algorithm - Phase 1: Check open blocks
// ============================================================================

ASTNode::Ptr StreamParser::check_open_blocks(const std::string &line,
                                             bool *all_matched, size_t &offset,
                                             size_t &column,
                                             bool &partially_consumed_tab,
                                             size_t &thematic_break_kill_pos) {
  bool should_continue = true;
  *all_matched = false;
  ASTNode::Ptr container = root_;

  while (last_child_is_open(container)) {
    container = container->last_child();
    NodeType cont_type = container->type();

    FirstNonspace fn = find_first_nonspace(line, offset, column);

    switch (cont_type) {
    case NodeType::BlockQuote:
      if (!parse_block_quote_prefix(line, offset, column,
                                    partially_consumed_tab, fn))
        goto done;
      break;

    case NodeType::List:
      // Lists don't have special continuation - items do
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
        std::string empty_line;
        container = finalize(container, 0, empty_line);
        if (!container) {
          container = root_;
        }
        return nullptr; // Signal stop
      }
      break;

    case NodeType::Heading:
      // Heading can never contain more than one line
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
  }

  *all_matched = true;

done:
  if (!*all_matched) {
    container = container->parent();
  }

  if (!should_continue) {
    return nullptr;
  }

  return container;
}

// ============================================================================
// Core algorithm - Phase 2: Open new blocks
// ============================================================================

void StreamParser::open_new_blocks(ASTNode::Ptr *container,
                                   const std::string &line, bool all_matched,
                                   size_t &offset, size_t &column,
                                   bool &partially_consumed_tab,
                                   size_t &thematic_break_kill_pos) {
  if (!*container)
    return;

  ASTNode::Ptr current_block = get_deepest_open_block();
  bool indented;
  ListMarkerInfo list_info{};
  bool maybe_lazy = current_block && current_block->type() == NodeType::Paragraph;
  NodeType cont_type = (*container)->type();
  size_t matched = 0;

  while (cont_type != NodeType::CodeBlock && cont_type != NodeType::HtmlBlock) {
    FirstNonspace fn = find_first_nonspace(line, offset, column);
    indented = fn.indent >= CODE_INDENT;

    // Block quote
    if (!indented && peek_at(line, fn.offset) == '>') {
      size_t blockquote_startpos = fn.offset;

      advance_offset(line, offset, column, fn.offset + 1 - offset, false,
                     partially_consumed_tab);
      if (scan::is_space_or_tab(peek_at(line, offset))) {
        advance_offset(line, offset, column, 1, true, partially_consumed_tab);
      }

      *container = add_child(*container, NodeType::BlockQuote,
                             static_cast<int>(blockquote_startpos + 1));
    }
    // ATX heading
    else if (!indented &&
             (matched = scan_atx_heading_start(line, fn.offset))) {
      size_t heading_startpos = fn.offset;

      advance_offset(line, offset, column, fn.offset + matched - offset, false,
                     partially_consumed_tab);
      *container = add_child(*container, NodeType::Heading,
                             static_cast<int>(heading_startpos + 1));

      HeadingData hdata{};
      hdata.level = static_cast<uint8_t>(matched);
      hdata.setext = false;
      (*container)->set_data(hdata);
    }
    // Fenced code block
    else if (!indented) {
      CodeFenceInfo fence_info{};
      matched = scan_open_code_fence(line, fn.offset, &fence_info);
      if (matched) {
        *container = add_child(*container, NodeType::CodeBlock,
                               static_cast<int>(fn.offset + 1));

        CodeData cdata{};
        cdata.fence_char = fence_info.fence_char;
        cdata.fence_length =
            static_cast<uint8_t>(std::min(fence_info.fence_length, size_t(255)));
        cdata.fence_offset = static_cast<uint8_t>(fn.offset - offset);
        cdata.info = fence_info.info;
        (*container)->set_data(cdata);

        advance_offset(line, offset, column, fn.offset + matched - offset,
                       false, partially_consumed_tab);
      } else {
        // HTML block
        HtmlBlockType html_type = scan_html_block_start(line, fn.offset);
        if (html_type != HtmlBlockType::None) {
          // Type 7 can't interrupt a paragraph
          if (html_type == HtmlBlockType::Type7 && maybe_lazy) {
            // Skip
          } else {
            *container = add_child(*container, NodeType::HtmlBlock,
                                   static_cast<int>(fn.offset + 1));
            (*container)->set_data(static_cast<int>(html_type));
            goto check_setext;
          }
        }

        // Setext heading
        char setext_char;
        if (cont_type == NodeType::Paragraph &&
            (matched = scan_setext_heading_line(line, fn.offset, &setext_char))) {
          // Convert paragraph to setext heading
          // Note: We need to change the type of the container
          // Since we can't change type directly, we store content and
          // recreate
          ASTNode::Ptr parent = (*container)->parent();
          int level = (setext_char == '=') ? 1 : 2;

          // Unlink the paragraph
          (*container)->unlink();

          // Create heading with the same content
          ASTNode::Ptr heading = add_child(parent, NodeType::Heading,
                                           (*container)->start_col());
          heading->set_start((*container)->start_line(), (*container)->start_col());

          HeadingData hdata{};
          hdata.level = static_cast<uint8_t>(level);
          hdata.setext = true;
          heading->set_data(hdata);

          // Move text from old container - content_ has accumulated text
          // The content was being accumulated in content_ for the paragraph

          *container = heading;
          advance_offset(line, offset, column, line.size() - 1 - offset, false,
                         partially_consumed_tab);
          goto after_block_checks;
        }

      check_setext:
        // Thematic break
        char thematic_char;
        if (!indented &&
            !(cont_type == NodeType::Paragraph && !all_matched) &&
            (matched = scan_thematic_break(line, fn.offset, &thematic_char))) {
          *container = add_child(*container, NodeType::ThematicBreak,
                                 static_cast<int>(fn.offset + 1));
          advance_offset(line, offset, column, line.size() - 1 - offset, false,
                         partially_consumed_tab);
        }
        // List item
        else if ((!indented || cont_type == NodeType::List) &&
                 fn.indent < 4 &&
                 (matched = scan_list_marker(line, fn.offset, &list_info))) {
          // Check if list marker can interrupt paragraph
          bool interrupts_paragraph =
              (*container)->type() == NodeType::Paragraph;
          if (interrupts_paragraph) {
            // Ordered list starting != 1 can't interrupt paragraph
            if (list_info.is_ordered && list_info.start_number != 1) {
              goto after_block_checks;
            }
            // Empty list item can't interrupt paragraph
            if (is_blank_line(line, fn.offset + matched)) {
              goto after_block_checks;
            }
          }

          advance_offset(line, offset, column, fn.offset + matched - offset,
                         false, partially_consumed_tab);

          // Create list data
          ListData ldata{};
          ldata.marker_char = list_info.marker_char;
          ldata.is_ordered = list_info.is_ordered;
          ldata.start = list_info.start_number;
          ldata.marker_offset = fn.indent;
          ldata.padding = static_cast<int>(list_info.padding);
          ldata.is_tight = true;

          // Check if we need a new list or can continue existing
          if (cont_type != NodeType::List) {
            *container = add_child(*container, NodeType::List,
                                   static_cast<int>(fn.offset + 1));
            (*container)->set_data(ldata);
          } else {
            const ListData *existing = (*container)->get_data<ListData>();
            if (!existing || !ldata.matches(*existing)) {
              *container = add_child(*container, NodeType::List,
                                     static_cast<int>(fn.offset + 1));
              (*container)->set_data(ldata);
            }
          }

          // Add list item
          *container = add_child(*container, NodeType::Item,
                                 static_cast<int>(fn.offset + 1));
          (*container)->set_data(ldata);
        }
        // Indented code block
        else if (indented && !maybe_lazy && !fn.blank) {
          advance_offset(line, offset, column, CODE_INDENT, true,
                         partially_consumed_tab);
          *container = add_child(*container, NodeType::CodeBlock,
                                 static_cast<int>(offset + 1));

          CodeData cdata{};
          cdata.fence_length = 0; // Not fenced
          cdata.fence_char = 0;
          cdata.fence_offset = 0;
          (*container)->set_data(cdata);
        } else {
          goto after_block_checks;
        }
      }
    }
    // Indented code (when line starts indented)
    else if (indented && !maybe_lazy && !fn.blank) {
      advance_offset(line, offset, column, CODE_INDENT, true,
                     partially_consumed_tab);
      *container = add_child(*container, NodeType::CodeBlock,
                             static_cast<int>(offset + 1));

      CodeData cdata{};
      cdata.fence_length = 0;
      cdata.fence_char = 0;
      cdata.fence_offset = 0;
      (*container)->set_data(cdata);
    } else {
      break;
    }

  after_block_checks:
    if (*container && accepts_lines((*container)->type())) {
      break;
    }

    cont_type = (*container)->type();
    maybe_lazy = false;
  }
}

// ============================================================================
// Core algorithm - Phase 3: Add text to container
// ============================================================================

void StreamParser::add_text_to_container(ASTNode::Ptr container,
                                         ASTNode::Ptr last_matched_container,
                                         const std::string &line,
                                         size_t &offset, size_t &column,
                                         bool &partially_consumed_tab,
                                         const FirstNonspace &fn) {
  if (fn.blank && container->last_child()) {
    container->last_child()->set_last_line_blank(true);
  }

  NodeType ctype = container->type();
  const CodeData *code = container->get_data<CodeData>();

  bool is_blank_allowed =
      (ctype != NodeType::BlockQuote && ctype != NodeType::Heading &&
       ctype != NodeType::ThematicBreak &&
       !(ctype == NodeType::CodeBlock && code && code->is_fenced()) &&
       !(ctype == NodeType::Item && !container->first_child() &&
         container->start_line() == current_line_));

  container->set_last_line_blank(fn.blank && is_blank_allowed);

  // Clear last_line_blank on all parents
  ASTNode::Ptr tmp = container;
  while (tmp->parent()) {
    tmp->parent()->set_last_line_blank(false);
    tmp = tmp->parent();
  }

  // Lazy continuation check
  ASTNode::Ptr current_block = get_deepest_open_block();
  if (current_block != last_matched_container &&
      container == last_matched_container && !fn.blank &&
      current_block && current_block->type() == NodeType::Paragraph) {
    add_line(line, offset, column, partially_consumed_tab);
  } else {
    // Finalize unmatched blocks
    std::string empty_line;
    while (current_block && current_block != last_matched_container) {
      current_block = finalize(current_block, 0, empty_line);
    }

    NodeType container_type = container->type();
    if (container_type == NodeType::CodeBlock) {
      add_line(line, offset, column, partially_consumed_tab);
    } else if (container_type == NodeType::HtmlBlock) {
      add_line(line, offset, column, partially_consumed_tab);

      // Check for HTML block end
      const int *html_type = container->get_data<int>();
      if (html_type) {
        if (scan_html_block_end(line, fn.offset,
                                static_cast<HtmlBlockType>(*html_type))) {
          finalize(container, 0, empty_line);
        }
      }
    } else if (fn.blank) {
      // Do nothing for blank lines
    } else if (accepts_lines(container_type)) {
      const HeadingData *heading = container->get_data<HeadingData>();
      if (container_type == NodeType::Heading && heading && !heading->setext) {
        // ATX heading - chop trailing hashtags
        std::string line_copy = line;
        chop_trailing_hashtags(line_copy);
        size_t temp_offset = fn.offset;
        size_t temp_column = fn.column;
        bool temp_partial = false;
        advance_offset(line_copy, temp_offset, temp_column, fn.offset - offset,
                       false, temp_partial);
        add_line(line_copy, temp_offset, temp_column, temp_partial);
      } else {
        advance_offset(line, offset, column, fn.offset - offset, false,
                       partially_consumed_tab);
        add_line(line, offset, column, partially_consumed_tab);
      }
    } else {
      // Create paragraph container
      container =
          add_child(container, NodeType::Paragraph, static_cast<int>(fn.offset + 1));
      advance_offset(line, offset, column, fn.offset - offset, false,
                     partially_consumed_tab);
      add_line(line, offset, column, partially_consumed_tab);
    }
  }
}

// ============================================================================
// Main entry point
// ============================================================================

void StreamParser::parse_line(const std::string &line) {
  std::string curline = line;

  // Ensure line ends with newline
  if (curline.empty() || !scan::is_line_end(curline.back())) {
    curline += '\n';
  }

  current_line_++;

  // Initialize parser state
  size_t offset = 0;
  size_t column = 0;
  bool partially_consumed_tab = false;
  size_t thematic_break_kill_pos = 0;

  bool all_matched = true;
  ASTNode::Ptr last_matched_container = check_open_blocks(
      curline, &all_matched, offset, column, partially_consumed_tab,
      thematic_break_kill_pos);

  if (last_matched_container) {
    ASTNode::Ptr container = last_matched_container;
    open_new_blocks(&container, curline, all_matched, offset, column,
                    partially_consumed_tab, thematic_break_kill_pos);

    FirstNonspace fn = find_first_nonspace(curline, offset, column);
    add_text_to_container(container, last_matched_container, curline, offset,
                          column, partially_consumed_tab, fn);
  }
}
