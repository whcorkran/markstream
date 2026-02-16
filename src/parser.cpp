#include "parser.hpp"
#include "ast_node.hpp"
#include "scanners.hpp"
#include <algorithm>

#define CODE_INDENT 4
#define TAB_STOP 4

Parser::Parser() : current_line_(0) {
  root_ = ASTNode::create(NodeType::Document, 1, 1);
  root_->set_open(true);
}

// get the deepest open block, will always parent newly created blocks
ASTNode::Ptr Parser::get_deepest_open_block() const {
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

// check if document has no open nested blocks
bool Parser::is_complete() const {
  if (!root_->is_open()) {
    return true;
  }
  ASTNode::Ptr last = root_->last_child();
  return !last || !last->is_open();
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
// Block creation
// ============================================================================

ASTNode::Ptr Parser::add_child(ASTNode::Ptr parent, NodeType block_type,
                               int start_column) {
  if (!parent)
    return nullptr;

  // If parent can't contain this child, finalize parent and move up
  while (!can_contain(parent->type(), block_type)) {
    parent = finalize(parent);
    if (!parent)
      return nullptr;
  }

  ASTNode::Ptr child = ASTNode::create(block_type, current_line_, start_column);
  parent->append_child(child);
  return child;
}

ASTNode::Ptr Parser::finalize(ASTNode::Ptr b) {
  if (!b || !b->is_open()) {
    return b ? b->parent() : nullptr;
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
      // Indented code: remove trailing blank lines
      std::string &text = const_cast<std::string &>(b->content());
      while (!text.empty() &&
             (text.back() == ' ' || text.back() == '\t' ||
              text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
      }
      text += '\n';
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
    // add space characters
    size_t chars_to_tab = TAB_STOP - (column % TAB_STOP);
    for (size_t i = 0; i < chars_to_tab; i++) {
      target->append_content(" ");
    }
  }

  if (offset < line.size()) {
    target->append_content(line.substr(offset));
  }
}

void Parser::chop_trailing_hashtags(std::string &line) const {
  // Remove trailing spaces
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }

  size_t orig_n = line.size();
  size_t n = orig_n;

  // Remove trailing #
  while (n > 0 && line[n - 1] == '#') {
    n--;
  }

  // Check for space before the final #
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

ASTNode::Ptr Parser::check_open_blocks(const std::string &line,
                                       bool *all_matched, size_t &offset,
                                       size_t &column,
                                       bool &partially_consumed_tab) {
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
        container = finalize(container);
        if (!container) {
          container = root_;
        }
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
  ctx.container =
      add_child(ctx.container, NodeType::BlockQuote, static_cast<int>(startpos + 1));
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

  // Advance past the entire opening fence line (info string is metadata, not content)
  advance_offset(ctx.line, ctx.offset, ctx.column,
                 ctx.line.size() - ctx.offset, false,
                 ctx.partially_consumed_tab);
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

  // Convert paragraph to setext heading
  ASTNode::Ptr parent = ctx.container->parent();
  int level = (setext_char == '=') ? 1 : 2;

  // Save content and position from paragraph before unlinking
  std::string para_content = std::move(const_cast<std::string &>(ctx.container->content()));
  int start_line = ctx.container->start_line();
  int start_col = ctx.container->start_col();

  ctx.container->unlink();

  // Create heading with the paragraph's content
  ASTNode::Ptr heading =
      add_child(parent, NodeType::Heading, start_col);
  heading->set_start(start_line, start_col);
  heading->set_content(std::move(para_content));

  HeadingData hdata{};
  hdata.level = static_cast<uint8_t>(level);
  hdata.setext = true;
  heading->set_data(hdata);

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
  size_t matched =
      scan_thematic_break(ctx.line, ctx.fn.offset, &thematic_char);
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

  advance_offset(ctx.line, ctx.offset, ctx.column,
                 ctx.fn.offset + matched - ctx.offset, false,
                 ctx.partially_consumed_tab);

  // Create list data
  ListData ldata{};
  ldata.marker_char = list_info.marker_char;
  ldata.is_ordered = list_info.is_ordered;
  ldata.start = list_info.start_number;
  ldata.marker_offset = ctx.fn.indent;
  ldata.padding = static_cast<int>(list_info.padding);
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

    OpenBlockCtx ctx{*container,   line,
                     offset,       column,
                     partially_consumed_tab, fn,
                     fn.indent >= CODE_INDENT, maybe_lazy,
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
      // Leaf blocks accept lines — done opening blocks
      if (result == BlockStart::Leaf || accepts_lines((*container)->type()))
        break;
      // Found a container block — loop to check for nested blocks
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

  // Clear last_line_blank on all parents
  ASTNode::Ptr tmp = container;
  while (tmp->parent()) {
    tmp->parent()->set_last_line_blank(false);
    tmp = tmp->parent();
  }

  // Lazy continuation check: if the deepest open block (from before phase 2)
  // was a paragraph that wasn't matched, and no new blocks were opened,
  // the line lazily continues the paragraph.
  if (deepest_before_new != last_matched_container &&
      container == last_matched_container && !fn.blank &&
      deepest_before_new &&
      deepest_before_new->type() == NodeType::Paragraph) {
    add_line(deepest_before_new, line, offset, column, partially_consumed_tab);
  } else {
    // Finalize unmatched blocks (from phase 1) that are still open
    ASTNode::Ptr current_block = deepest_before_new;
    while (current_block && current_block != last_matched_container) {
      current_block = finalize(current_block);
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
        add_line(container, line_copy, fn.offset, fn.column, false);
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

void Parser::parse_line(const std::string &line) {
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

  bool all_matched = true;
  ASTNode::Ptr last_matched_container = check_open_blocks(
      curline, &all_matched, offset, column, partially_consumed_tab);

  if (last_matched_container) {
    // Save the deepest open block before phase 2 creates new blocks.
    // Phase 3 needs this to finalize unmatched blocks correctly.
    ASTNode::Ptr deepest_before_new = get_deepest_open_block();

    ASTNode::Ptr container = last_matched_container;
    open_new_blocks(&container, curline, all_matched, offset, column,
                    partially_consumed_tab);

    FirstNonspace fn = find_first_nonspace(curline, offset, column);
    add_text_to_container(container, last_matched_container,
                          deepest_before_new, curline, offset, column,
                          partially_consumed_tab, fn);
  }
}
