#include "parser.hpp"
#include "tree.hpp"
#include <cmark.h>
#include <scanners.h>
#include <chunk.h>
#include <cmark_ctype.h>
#include <cstring>

#define CODE_INDENT 4
#define TAB_STOP 4

// Helper macros
#define MIN(x, y) ((x < y) ? x : y)

StreamParser::StreamParser() 
    : mem(cmark_get_default_mem_allocator()),
      root(mem, BlockType::Document, 1, 1)
{
    root.set_open(true);
}

Node StreamParser::get_deepest_open_block() const {
    Node current = root;
    while (true) {
        Node last_child = current.get_last_child();
        if (last_child.is_null() || !last_child.is_open()) {
            break;
        }
        current = last_child;
    }
    return current;
}

StreamParser::~StreamParser() {
    // Nodes are owned by cmark and will be freed when root is freed
    if (!root.is_null()) {
        cmark_node_free(root.get_cmark_node());
    }
}

// ============================================================================
// Helper functions
// ============================================================================

static Node find_deepest_open_block(Node root) {
    Node current = root;
    while (true) {
        Node last_child = current.get_last_child();
        if (last_child.is_null() || !last_child.is_open()) {
            break;
        }
        current = last_child;
    }
    return current;
}

static int get_max_line_number(Node root) {
    if (root.is_null()) {
        return 1;
    }
    
    int max_line = root.get_end_line();
    
    // Recursively check all children
    Node child = root.get_first_child();
    while (!child.is_null()) {
        int child_max = get_max_line_number(child);
        if (child_max > max_line) {
            max_line = child_max;
        }
        child = child.get_next();
    }
    
    return max_line > 0 ? max_line : 1;
}

bool StreamParser::is_complete() const {
    // Document is complete when root is closed, or deepest open block equals root and is closed
    if (!root.is_open()) {
        return true;
    }
    Node deepest = get_deepest_open_block();
    return deepest == root && !deepest.is_open();
}

// ============================================================================
// Line processing helpers
// ============================================================================

bool StreamParser::is_line_end_char(char c) const {
    return (c == '\n' || c == '\r');
}

bool StreamParser::is_space_or_tab(char c) const {
    return (c == ' ' || c == '\t');
}

bool StreamParser::is_blank(const std::string& line, size_t offset) const {
    for (size_t i = offset; i < line.size(); ++i) {
        switch (line[i]) {
        case '\r':
        case '\n':
            return true;
        case ' ':
            break;
        case '\t':
            break;
        default:
            return false;
        }
    }
    return true;
}

char StreamParser::peek_at(const std::string& input, size_t pos) const {
    if (pos >= input.size()) return '\0';
    return input[pos];
}

StreamParser::FirstNonspace StreamParser::find_first_nonspace(const std::string& line, size_t offset, size_t column) const {
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
    
    result.indent = result.column - column;
    result.blank = is_line_end_char(peek_at(line, result.offset));
    return result;
}

void StreamParser::advance_offset(const std::string& line, size_t& offset, size_t& column, size_t count, bool columns, bool& partially_consumed_tab) const {
    while (count > 0 && offset < line.size()) {
        char c = line[offset];
        if (c == '\t') {
            int chars_to_tab = TAB_STOP - (column % TAB_STOP);
            if (columns) {
                partially_consumed_tab = chars_to_tab > count;
                int chars_to_advance = MIN(count, chars_to_tab);
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
            column += 1; // assume ascii; block starts are ascii
            count -= 1;
        }
    }
}

// ============================================================================
// Block type checks
// ============================================================================

bool StreamParser::can_contain(BlockType parent_type, BlockType child_type) const {
    return (parent_type == BlockType::Document ||
            parent_type == BlockType::BlockQuote ||
            parent_type == BlockType::Item ||
            (parent_type == BlockType::List && child_type == BlockType::Item));
}

bool StreamParser::accepts_lines(BlockType block_type) const {
    return (block_type == BlockType::Paragraph ||
            block_type == BlockType::Heading ||
            block_type == BlockType::CodeBlock);
}

bool StreamParser::last_child_is_open(Node container) const {
    if (container.is_null()) return false;
    Node last_child = container.get_last_child();
    return !last_child.is_null() && last_child.is_open();
}

bool StreamParser::last_line_blank(Node node) const {
    return node.last_line_blank();
}

void StreamParser::set_last_line_blank(Node node, bool is_blank) {
    node.set_last_line_blank(is_blank);
}

// ============================================================================
// Block creation
// ============================================================================

Node StreamParser::make_block(BlockType tag, int start_column, int line_number) const {
    return Node(mem, tag, line_number, start_column);
}

Node StreamParser::add_child(Node parent, BlockType block_type, int start_column, int line_number) {
    if (parent.is_null()) return Node();
    
    // If parent can't contain this child, finalize parent and move up
    // When called from open_new_blocks, we're processing a line, so curline is not empty
    std::string empty_line; // Indicates not end of input (we're mid-line)
    while (!can_contain(parent.get_type(), block_type)) {
        parent = finalize(parent, line_number, 0, empty_line);
        if (parent.is_null()) return Node();
    }
    
    Node child = make_block(block_type, start_column, line_number);
    if (child.is_null()) return Node();
    
    child.set_parent(parent);
    
    Node last_child = parent.get_last_child();
    if (!last_child.is_null()) {
        last_child.set_next(child);
        child.set_prev(last_child);
    } else {
        parent.set_first_child(child);
    }
    parent.set_last_child(child);
    
    return child;
}

Node StreamParser::finalize(Node b, int line_number, size_t last_line_length, const std::string& curline) {
    if (b.is_null() || !b.is_open()) {
        return b.get_parent();
    }
    
    b.set_open(false);
    
    // Set end position
    if (curline.empty()) {
        // end of input - line number has not been incremented
        b.set_end_line(line_number);
        b.set_end_column(last_line_length);
    } else {
        BlockType btype = b.get_type();
        if (btype == BlockType::Document ||
            (btype == BlockType::CodeBlock && b.get_code_metadata().fenced) ||
            (btype == BlockType::Heading && b.get_heading_metadata().setext)) {
            b.set_end_line(line_number);
            size_t end_col = curline.size();
            if (end_col > 0 && curline[end_col - 1] == '\n') end_col -= 1;
            if (end_col > 0 && curline[end_col - 1] == '\r') end_col -= 1;
            b.set_end_column(end_col);
        } else {
            b.set_end_line(line_number - 1);
            b.set_end_column(last_line_length);
        }
    }
    
    // Process content based on block type
    BlockType btype = b.get_type();
    
    switch (btype) {
    case BlockType::Paragraph: {
        // For paragraphs, we store the content as data
        // Note: reference link resolution would happen here in full cmark
        if (!content.empty()) {
            b.set_data(content.c_str(), content.size());
            content.clear();
        }
        break;
    }
    
    case BlockType::CodeBlock: {
        auto code_meta = b.get_code_metadata();
        if (!code_meta.fenced) {
            // Indented code: remove trailing blank lines
            while (!content.empty() && 
                   (content.back() == ' ' || content.back() == '\t' || 
                    content.back() == '\n' || content.back() == '\r')) {
                content.pop_back();
            }
            content += '\n';
        } else {
            // Fenced code: first line becomes info
            size_t pos = 0;
            while (pos < content.size() && !is_line_end_char(content[pos])) {
                pos++;
            }
            
            if (pos > 0) {
                std::string info = content.substr(0, pos);
                // Trim and unescape would happen here
                // For now, just store as-is
                b.set_data(info.c_str(), info.size());
            }
            
            // Skip newline
            if (pos < content.size() && content[pos] == '\r') pos++;
            if (pos < content.size() && content[pos] == '\n') pos++;
            content.erase(0, pos);
        }
        
        if (!content.empty()) {
            // Store remaining content
            std::string existing = b.get_data() ? std::string(b.get_data(), b.get_data_len()) : "";
            existing += content;
            b.set_data(existing.c_str(), existing.size());
            content.clear();
        }
        break;
    }
    
    case BlockType::Heading:
    case BlockType::HtmlBlock:
        if (!content.empty()) {
            b.set_data(content.c_str(), content.size());
            content.clear();
        }
        break;
    
    case BlockType::List: {
        // Determine tight/loose status
        ListMetadata list_meta = b.get_list_metadata();
        list_meta.tight = true; // tight by default
        
        Node item = b.get_first_child();
        while (!item.is_null()) {
            if (item.last_line_blank() && !item.get_next().is_null()) {
                list_meta.tight = false;
                break;
            }
            
            // Check children of list item
            Node subitem = item.get_first_child();
            while (!subitem.is_null()) {
                // Simplified check - would need recursive blank line check
                if ((!item.get_next().is_null() || !subitem.get_next().is_null()) &&
                    subitem.last_line_blank()) {
                    list_meta.tight = false;
                    break;
                }
                subitem = subitem.get_next();
            }
            if (!list_meta.tight) break;
            item = item.get_next();
        }
        
        b.set_list_metadata(list_meta);
        break;
    }
    
    default:
        break;
    }
    
    return b.get_parent();
}

// ============================================================================
// Block continuation checkers
// ============================================================================

bool StreamParser::parse_block_quote_prefix(const std::string& line, size_t& offset, size_t& column, bool& partially_consumed_tab, const FirstNonspace& fn) const {
    if (fn.indent <= 3 && peek_at(line, fn.offset) == '>') {
        advance_offset(line, offset, column, fn.indent + 1, true, partially_consumed_tab);
        
        if (is_space_or_tab(peek_at(line, offset))) {
            advance_offset(line, offset, column, 1, true, partially_consumed_tab);
        }
        
        return true;
    }
    return false;
}

bool StreamParser::parse_node_item_prefix(const std::string& line, Node container, size_t& offset, size_t& column, bool& partially_consumed_tab, const FirstNonspace& fn) const {
    if (container.is_null()) return false;
    
    ListMetadata list_meta = container.get_list_metadata();
    
    if (fn.indent >= list_meta.marker_offset + list_meta.padding) {
        advance_offset(line, offset, column, list_meta.marker_offset + list_meta.padding, true, partially_consumed_tab);
        return true;
    } else if (fn.blank && !container.get_first_child().is_null()) {
        // Lazy continuation
        advance_offset(line, offset, column, fn.offset - offset, false, partially_consumed_tab);
        return true;
    }
    return false;
}

bool StreamParser::parse_code_block_prefix(const std::string& line, Node container, size_t& offset, size_t& column, bool& partially_consumed_tab, bool* should_continue, const FirstNonspace& fn) const {
    if (container.is_null()) return false;
    
    auto code_meta = container.get_code_metadata();
    
    if (!code_meta.fenced) {
        // Indented code
        if (fn.indent >= CODE_INDENT) {
            advance_offset(line, offset, column, CODE_INDENT, true, partially_consumed_tab);
            return true;
        } else if (fn.blank) {
            advance_offset(line, offset, column, fn.offset - offset, false, partially_consumed_tab);
            return true;
        }
    } else {
        // Fenced code
        size_t matched = 0;
        if (fn.indent <= 3 && peek_at(line, fn.offset) == code_meta.fence_char) {
            cmark_chunk ch = to_chunk(line);
            matched = scan_close_code_fence(&ch, fn.offset);
        }
        
        if (matched >= code_meta.fence_length) {
            // Closing fence - note: finalize will be called by caller
            *should_continue = false;
            advance_offset(line, offset, column, matched, false, partially_consumed_tab);
            return true;
        } else {
            // Skip optional spaces of fence offset
            int i = code_meta.fence_offset;
            while (i > 0 && is_space_or_tab(peek_at(line, offset))) {
                advance_offset(line, offset, column, 1, true, partially_consumed_tab);
                i--;
            }
            return true;
        }
    }
    return false;
}

bool StreamParser::parse_html_block_prefix(Node container, const FirstNonspace& fn) const {
    if (container.is_null()) return false;
    
    int html_block_type = container.get_html_block_type();
    if (html_block_type < 1 || html_block_type > 7) return false;
    
    switch (html_block_type) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        // These types can accept blanks
        return true;
    case 6:
    case 7:
        return !fn.blank;
    default:
        return false;
    }
}

// ============================================================================
// List parsing
// ============================================================================


size_t StreamParser::parse_list_marker(const std::string& input, size_t pos, bool interrupts_paragraph, ListMetadata& data) const {
    size_t startpos = pos;
    char c = peek_at(input, pos);

    if (c == '*' || c == '-' || c == '+') {
        pos++;
        if (!cmark_isspace(peek_at(input, pos))) {
            return 0;
        }

        if (interrupts_paragraph) {
            size_t i = pos;
            while (is_space_or_tab(peek_at(input, i))) {
                i++;
            }
            if (peek_at(input, i) == '\n') {
                return 0;
            }
        }

        data.marker_offset = 0; // will be adjusted later
        data.list_type = ListType::Bullet;
        data.bullet_char = c;
        data.start = 0;
        data.delimiter = DelimType::None;
        data.tight = false;
        return pos - startpos;
    } else if (cmark_isdigit(c)) {
        int start = 0;
        int digits = 0;

        do {
            start = (10 * start) + (peek_at(input, pos) - '0');
            pos++;
            digits++;
            if (digits >= 9) break; // limit to avoid overflow
        } while (digits < 9 && cmark_isdigit(peek_at(input, pos)));

        if (interrupts_paragraph && start != 1) {
            return 0;
        }

        c = peek_at(input, pos);
        if (c == '.' || c == ')') {
            pos++;
            if (!cmark_isspace(peek_at(input, pos))) {
                return 0;
            }
            if (interrupts_paragraph) {
                size_t i = pos;
                while (is_space_or_tab(peek_at(input, i))) {
                    i++;
                }
                if (is_line_end_char(peek_at(input, i))) {
                    return 0;
                }
            }

            data.marker_offset = 0;
            data.list_type = ListType::Ordered;
            data.bullet_char = 0;
            data.start = start;
            data.delimiter = (c == '.' ? DelimType::Period : DelimType::Paren);
            data.tight = false;
            return pos - startpos;
        }
    }

    return 0;
}

// ============================================================================
// Thematic break
// ============================================================================

size_t StreamParser::scan_thematic_break(const std::string& input, size_t offset, size_t& kill_pos) const {
    size_t i = offset;
    char c = peek_at(input, i);
    
    if (!(c == '*' || c == '_' || c == '-')) {
        kill_pos = i;
        return 0;
    }
    
    int count = 1;
    char nextc = '\0';
    while ((nextc = peek_at(input, ++i))) {
        if (nextc == c) {
            count++;
        } else if (nextc != ' ' && nextc != '\t') {
            break;
        }
    }
    
    if (count >= 3 && (nextc == '\r' || nextc == '\n' || nextc == '\0')) {
        return (i - offset) + (nextc != '\0' ? 1 : 0);
    } else {
        kill_pos = i;
        return 0;
    }
}

void StreamParser::chop_trailing_hashtags(std::string& line) const {
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
    if (n != orig_n && n > 0 && is_space_or_tab(line[n - 1])) {
        line.erase(n - 1);
        // Remove trailing spaces again
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
    }
}

// ============================================================================
// Text accumulation
// ============================================================================

void StreamParser::add_line(const std::string& line, size_t offset, size_t column, bool partially_consumed_tab) {
    if (partially_consumed_tab) {
        size_t temp_offset = offset + 1; // skip over tab
        // add space characters
        int chars_to_tab = TAB_STOP - (column % TAB_STOP);
        for (int i = 0; i < chars_to_tab; i++) {
            content += ' ';
        }
    }
    
    if (offset < line.size()) {
        content += line.substr(offset);
    }
}

// ============================================================================
// Three-phase algorithm
// ============================================================================

Node StreamParser::check_open_blocks(const std::string& line, bool* all_matched, size_t& offset, size_t& column, bool& partially_consumed_tab, size_t& thematic_break_kill_pos, int line_number) {
    bool should_continue = true;
    *all_matched = false;
    Node container = root;
    
    while (last_child_is_open(container)) {
        container = container.get_last_child();
        BlockType cont_type = container.get_type();
        
        FirstNonspace fn = find_first_nonspace(line, offset, column);
        
        switch (cont_type) {
        case BlockType::BlockQuote:
            if (!parse_block_quote_prefix(line, offset, column, partially_consumed_tab, fn))
                goto done;
            break;
            
        case BlockType::List:
            // Lists handle blank lines specially
            if (fn.blank) {
                // Simplified - would need to track last line blank flag
            }
            break;
            
        case BlockType::Item:
            if (!parse_node_item_prefix(line, container, offset, column, partially_consumed_tab, fn))
                goto done;
            break;
            
        case BlockType::CodeBlock:
            if (!parse_code_block_prefix(line, container, offset, column, partially_consumed_tab, &should_continue, fn))
                goto done;
            if (!should_continue) {
                // Need to finalize the container
                std::string empty_line;
                container = finalize(container, line_number, 0, empty_line);
                if (container.is_null()) {
                    container = root;
                }
                return Node(); // null node to signal stop
            }
            break;
            
        case BlockType::Heading:
            // Heading can never contain more than one line
            goto done;
            
        case BlockType::HtmlBlock:
            if (!parse_html_block_prefix(container, fn))
                goto done;
            break;
            
        case BlockType::Paragraph:
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
        container = container.get_parent(); // back up to last matching node
    }
    
    if (!should_continue) {
        return Node(); // null node
    }
    
    return container;
}

void StreamParser::open_new_blocks(Node* container, const std::string& line, bool all_matched, size_t& offset, size_t& column, bool& partially_consumed_tab, size_t& thematic_break_kill_pos, int line_number) {
    if (container->is_null()) return;
    
    Node current_block = find_deepest_open_block(root);
    bool indented;
    ListMetadata data;
    bool maybe_lazy = current_block.get_type() == BlockType::Paragraph;
    BlockType cont_type = container->get_type();
    size_t matched = 0;
    int lev = 0;
    bool save_partially_consumed_tab;
    size_t save_offset;
    size_t save_column;
    
    while (cont_type != BlockType::CodeBlock && cont_type != BlockType::HtmlBlock) {
        FirstNonspace fn = find_first_nonspace(line, offset, column);
        indented = fn.indent >= CODE_INDENT;
        
        if (!indented && peek_at(line, fn.offset) == '>') {
            size_t blockquote_startpos = fn.offset;
            
            advance_offset(line, offset, column, fn.offset + 1 - offset, false, partially_consumed_tab);
            if (is_space_or_tab(peek_at(line, offset))) {
                advance_offset(line, offset, column, 1, true, partially_consumed_tab);
            }
            
            *container = add_child(*container, BlockType::BlockQuote, blockquote_startpos + 1, line_number);
            
        } else {
            cmark_chunk ch = to_chunk(line);
            if (!indented && (matched = scan_atx_heading_start(&ch, fn.offset))) {
                size_t hashpos;
                int level = 0;
                size_t heading_startpos = fn.offset;
                
                advance_offset(line, offset, column, fn.offset + matched - offset, false, partially_consumed_tab);
                *container = add_child(*container, BlockType::Heading, heading_startpos + 1, line_number);
                
                hashpos = cmark_chunk_strchr(&ch, '#', fn.offset);
            
            while (hashpos < line.size() && line[hashpos] == '#') {
                level++;
                hashpos++;
            }
            
            Node::HeadingMetadata heading_meta;
            heading_meta.level = level;
            heading_meta.setext = false;
            heading_meta.internal_offset = matched;
            container->set_heading_metadata(heading_meta);
            } else if (!indented && (matched = scan_open_code_fence(&ch, fn.offset))) {
            *container = add_child(*container, BlockType::CodeBlock, fn.offset + 1, line_number);
            
            Node::CodeMetadata code_meta;
            code_meta.fenced = true;
            code_meta.fence_char = peek_at(line, fn.offset);
            code_meta.fence_length = (matched > 255) ? 255 : matched;
            code_meta.fence_offset = fn.offset - offset;
            code_meta.info = nullptr;
            container->set_code_metadata(code_meta);
            
            advance_offset(line, offset, column, fn.offset + matched - offset, false, partially_consumed_tab);
            } else if (!indented && ((matched = scan_html_block_start(&ch, fn.offset)) ||
                                 (cont_type != BlockType::Paragraph && !maybe_lazy &&
                                  (matched = scan_html_block_start_7(&ch, fn.offset))))) {
            *container = add_child(*container, BlockType::HtmlBlock, fn.offset + 1, line_number);
            container->set_html_block_type(matched);
            } else if (!indented && cont_type == BlockType::Paragraph &&
                   (lev = scan_setext_heading_line(&ch, fn.offset))) {
            // Convert paragraph to heading
            container->set_type(BlockType::Heading);
            Node::HeadingMetadata heading_meta;
            heading_meta.level = lev;
            heading_meta.setext = true;
            container->set_heading_metadata(heading_meta);
            advance_offset(line, offset, column, line.size() - 1 - offset, false, partially_consumed_tab);
            
        } else if (!indented &&
                   !(cont_type == BlockType::Paragraph && !all_matched) &&
                   (thematic_break_kill_pos <= fn.offset) &&
                   (matched = scan_thematic_break(line, fn.offset, thematic_break_kill_pos))) {
            *container = add_child(*container, BlockType::ThematicBreak, fn.offset + 1, line_number);
            advance_offset(line, offset, column, line.size() - 1 - offset, false, partially_consumed_tab);
            
        } else if ((!indented || cont_type == BlockType::List) &&
                   fn.indent < 4 &&
                   (matched = parse_list_marker(line, fn.offset,
                                                container->get_type() == BlockType::Paragraph, data))) {
            
            // Compute padding
            advance_offset(line, offset, column, fn.offset + matched - offset, false, partially_consumed_tab);
            
            save_partially_consumed_tab = partially_consumed_tab;
            save_offset = offset;
            save_column = column;
            
            while (column - save_column <= 5 && is_space_or_tab(peek_at(line, offset))) {
                advance_offset(line, offset, column, 1, true, partially_consumed_tab);
            }
            
            int i = column - save_column;
            if (i >= 5 || i < 1 || is_line_end_char(peek_at(line, offset))) {
                data.padding = matched + 1;
                offset = save_offset;
                column = save_column;
                partially_consumed_tab = save_partially_consumed_tab;
                if (i > 0) {
                    advance_offset(line, offset, column, 1, true, partially_consumed_tab);
                }
            } else {
                data.padding = matched + i;
            }
            
            data.marker_offset = fn.indent;
            
            // Check if we can continue existing list
            if (cont_type != BlockType::List || 
                !data.matches(container->get_list_metadata())) {
                *container = add_child(*container, BlockType::List, fn.offset + 1, line_number);
                container->set_list_metadata(data);
            }
            
            // Add the list item
            *container = add_child(*container, BlockType::Item, fn.offset + 1, line_number);
            container->set_list_metadata(data);
            
        } else if (indented && !maybe_lazy && !fn.blank) {
            advance_offset(line, offset, column, CODE_INDENT, true, partially_consumed_tab);
            *container = add_child(*container, BlockType::CodeBlock, offset + 1, line_number);
            
            Node::CodeMetadata code_meta;
            code_meta.fenced = false;
            code_meta.fence_char = 0;
            code_meta.fence_length = 0;
            code_meta.fence_offset = 0;
            code_meta.info = nullptr;
            container->set_code_metadata(code_meta);
            
            } else {
                break;
            }
        }
        
        if (accepts_lines(container->get_type())) {
            break;
        }
        
        cont_type = container->get_type();
        maybe_lazy = false;
    }
}

void StreamParser::add_text_to_container(Node container, Node last_matched_container, const std::string& line, size_t& offset, size_t& column, bool& partially_consumed_tab, int line_number, const FirstNonspace& fn) {
    if (fn.blank && !container.get_last_child().is_null()) {
        set_last_line_blank(container.get_last_child(), true);
    }
    
    BlockType ctype = container.get_type();
    bool last_line_blank = (fn.blank && ctype != BlockType::BlockQuote &&
                           ctype != BlockType::Heading && ctype != BlockType::ThematicBreak &&
                           !(ctype == BlockType::CodeBlock && container.get_code_metadata().fenced) &&
                           !(ctype == BlockType::Item && container.get_first_child().is_null() &&
                             container.get_start_line() == line_number));
    
    set_last_line_blank(container, last_line_blank);
    
    // Clear last_line_blank on all parents
    Node tmp = container;
    while (!tmp.get_parent().is_null()) {
        set_last_line_blank(tmp.get_parent(), false);
        tmp = tmp.get_parent();
    }
    
    // Lazy continuation check
    Node current_block = find_deepest_open_block(root);
    if (current_block != last_matched_container &&
        container == last_matched_container && !fn.blank &&
        current_block.get_type() == BlockType::Paragraph) {
        add_line(line, offset, column, partially_consumed_tab);
    } else {
        // Finalize unmatched blocks
        std::string empty_line;
        while (current_block != last_matched_container) {
            current_block = finalize(current_block, line_number, 0, empty_line);
            if (current_block.is_null()) {
                current_block = root;
                break;
            }
        }
        
        BlockType container_type = container.get_type();
        if (container_type == BlockType::CodeBlock) {
            add_line(line, offset, column, partially_consumed_tab);
        } else if (container_type == BlockType::HtmlBlock) {
            add_line(line, offset, column, partially_consumed_tab);
            
            // Check for HTML block end conditions
            int matches_end_condition = 0;
            int html_block_type = container.get_html_block_type();
            cmark_chunk ch = to_chunk(line);
            
            switch (html_block_type) {
            case 1:
                matches_end_condition = scan_html_block_end_1(&ch, fn.offset);
                break;
            case 2:
                matches_end_condition = scan_html_block_end_2(&ch, fn.offset);
                break;
            case 3:
                matches_end_condition = scan_html_block_end_3(&ch, fn.offset);
                break;
            case 4:
                matches_end_condition = scan_html_block_end_4(&ch, fn.offset);
                break;
            case 5:
                matches_end_condition = scan_html_block_end_5(&ch, fn.offset);
                break;
            default:
                break;
            }
            
            if (matches_end_condition) {
                container = finalize(container, line_number, 0, empty_line);
                if (container.is_null()) {
                    container = root;
                }
            }
        } else if (fn.blank) {
            // Do nothing for blank lines
        } else if (accepts_lines(container_type)) {
            if (container_type == BlockType::Heading && !container.get_heading_metadata().setext) {
                std::string line_copy = line;
                chop_trailing_hashtags(line_copy);
                size_t temp_offset = fn.offset;
                size_t temp_column = fn.column;
                bool temp_partial = false;
                advance_offset(line_copy, temp_offset, temp_column, fn.offset - offset, false, temp_partial);
                add_line(line_copy, temp_offset, temp_column, temp_partial);
            } else {
                advance_offset(line, offset, column, fn.offset - offset, false, partially_consumed_tab);
                add_line(line, offset, column, partially_consumed_tab);
            }
        } else {
            // Create paragraph container
            container = add_child(container, BlockType::Paragraph, fn.offset + 1, line_number);
            advance_offset(line, offset, column, fn.offset - offset, false, partially_consumed_tab);
            add_line(line, offset, column, partially_consumed_tab);
        }
    }
}

// ============================================================================
// Main entry point
// ============================================================================

void StreamParser::parse_line(const std::string& line) {
    std::string curline = line;
    
    // Ensure line ends with newline
    if (curline.empty() || !is_line_end_char(curline.back())) {
        curline += '\n';
    }
    
    // Compute line number from tree
    int line_number = get_max_line_number(root) + 1;
    
    // Initialize parser state (local variables)
    size_t offset = 0;
    size_t column = 0;
    bool partially_consumed_tab = false;
    size_t thematic_break_kill_pos = 0;
    
    bool all_matched = true;
    Node last_matched_container = check_open_blocks(curline, &all_matched, offset, column, partially_consumed_tab, thematic_break_kill_pos, line_number);
    
    if (!last_matched_container.is_null()) {
        Node container = last_matched_container;
        open_new_blocks(&container, curline, all_matched, offset, column, partially_consumed_tab, thematic_break_kill_pos, line_number);
        
        // Compute first_nonspace for add_text_to_container
        FirstNonspace fn = find_first_nonspace(curline, offset, column);
        add_text_to_container(container, last_matched_container, curline, offset, column, partially_consumed_tab, line_number, fn);
    }
}

