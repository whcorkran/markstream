#ifndef STREAMING_PARSER_H
#define STREAMING_PARSER_H

#include <buffer.h>
#include <chunk.h> // Only needed for cmark_chunk type in helper functions
#include <cmark.h>
#include <cmark_ctype.h>
#include <node.h>
#include <parser.h>
#include <scanners.h>
#include <string>

// ============================================================================
// C++ replacements for cmark data structures
// These will replace cmark types when we remove the dependency
// ============================================================================

// List metadata - C++ replacement for cmark_list
struct ListMetadata {
  int marker_offset = 0;
  int padding = 0;
  int start = 0;
  unsigned char list_type = 0;
  unsigned char delimiter = 0;
  unsigned char bullet_char = 0;
  bool tight = false;

  // Convert to cmark_list for assignment to cmark_node
  // Temporary until we remove cmark dependency
  cmark_list to_cmark_list() const {
    cmark_list result;
    result.marker_offset = marker_offset;
    result.padding = padding;
    result.start = start;
    result.list_type = list_type;
    result.delimiter = delimiter;
    result.bullet_char = bullet_char;
    result.tight = tight;
    return result;
  }

  // Create from cmark_list (for reading from cmark_node)
  // Temporary until we remove cmark dependency
  static ListMetadata from_cmark_list(const cmark_list &list) {
    ListMetadata result;
    result.marker_offset = list.marker_offset;
    result.padding = list.padding;
    result.start = list.start;
    result.list_type = list.list_type;
    result.delimiter = list.delimiter;
    result.bullet_char = list.bullet_char;
    result.tight = list.tight;
    return result;
  }

  // Check if two list metadata match (for list continuation)
  bool matches(const ListMetadata &other) const {
    return list_type == other.list_type && delimiter == other.delimiter &&
           bullet_char == other.bullet_char;
  }
};

// Helper to convert std::string to cmark_chunk for scanner function calls
// Temporary until we remove cmark dependency
inline cmark_chunk to_chunk(const std::string &s) {
  cmark_chunk ch;
  ch.data = reinterpret_cast<const unsigned char *>(s.data());
  ch.len = static_cast<bufsize_t>(s.size());
  return ch;
}

// Block type enum
enum class BlockType {
  Document,
  BlockQuote,
  List,
  Item,
  CodeBlock,
  Heading,
  HtmlBlock,
  Paragraph,
  ThematicBreak
};

// TODO: remove dependency on cmark_node_type

// Convert BlockType to cmark_node_type
inline cmark_node_type to_cmark_node_type(BlockType type) {
  switch (type) {
  case BlockType::Document:
    return CMARK_NODE_DOCUMENT;
  case BlockType::BlockQuote:
    return CMARK_NODE_BLOCK_QUOTE;
  case BlockType::List:
    return CMARK_NODE_LIST;
  case BlockType::Item:
    return CMARK_NODE_ITEM;
  case BlockType::CodeBlock:
    return CMARK_NODE_CODE_BLOCK;
  case BlockType::Heading:
    return CMARK_NODE_HEADING;
  case BlockType::HtmlBlock:
    return CMARK_NODE_HTML_BLOCK;
  case BlockType::Paragraph:
    return CMARK_NODE_PARAGRAPH;
  case BlockType::ThematicBreak:
    return CMARK_NODE_THEMATIC_BREAK;
  default:
    return CMARK_NODE_DOCUMENT;
  }
}

// Convert cmark_node_type to BlockType (for reading from nodes)
inline BlockType from_cmark_node_type(cmark_node_type type) {
  switch (type) {
  case CMARK_NODE_DOCUMENT:
    return BlockType::Document;
  case CMARK_NODE_BLOCK_QUOTE:
    return BlockType::BlockQuote;
  case CMARK_NODE_LIST:
    return BlockType::List;
  case CMARK_NODE_ITEM:
    return BlockType::Item;
  case CMARK_NODE_CODE_BLOCK:
    return BlockType::CodeBlock;
  case CMARK_NODE_HEADING:
    return BlockType::Heading;
  case CMARK_NODE_HTML_BLOCK:
    return BlockType::HtmlBlock;
  case CMARK_NODE_PARAGRAPH:
    return BlockType::Paragraph;
  case CMARK_NODE_THEMATIC_BREAK:
    return BlockType::ThematicBreak;
  default:
    return BlockType::Document; // fallback
  }
}

class StreamingParser {
public:
  explicit StreamingParser();
  ~StreamingParser();

  void parse_line(const std::string &line);

  cmark_node *get_root() const { return root; }

  cmark_node *get_current() const { return current; }

  bool is_complete() const;

  cmark_mem *get_mem() const {
    return mem;
  } // Temporary - needed for cmark_node allocation

private:
  // Memory allocator
  cmark_mem *mem;

  // AST root and current position
  cmark_node *root;
  cmark_node *current;

  // Parser state
  int line_number;
  size_t offset;
  size_t column;
  size_t first_nonspace;
  size_t first_nonspace_column;
  size_t thematic_break_kill_pos;
  int indent;
  bool blank;
  bool partially_consumed_tab;
  size_t last_line_length;

  // Content buffer for accumulating text (replaces cmark_strbuf)
  std::string content;

  // Current line (stored as string for easier manipulation)
  std::string current_line;

  // Helper functions for line processing
  void find_first_nonspace(const std::string &input);
  void advance_offset(const std::string &input, size_t count, bool columns);
  bool is_line_end_char(char c) const;
  bool is_space_or_tab(char c) const;
  bool is_blank(const std::string &s, size_t offset) const;

  // Block continuation checks
  bool parse_block_quote_prefix(const std::string &input);
  bool parse_node_item_prefix(const std::string &input, cmark_node *container);
  bool parse_code_block_prefix(const std::string &input, cmark_node *container,
                               bool *should_continue);
  bool parse_html_block_prefix(cmark_node *container);

  // Block creation
  cmark_node *make_block(BlockType tag, int start_column);
  cmark_node *add_child(cmark_node *parent, BlockType block_type,
                        int start_column);
  cmark_node *finalize(cmark_node *b);

  // Block type checks
  bool can_contain(BlockType parent_type, BlockType child_type) const;
  bool accepts_lines(BlockType block_type) const;
  bool last_child_is_open(cmark_node *container) const;

  // Node flag helpers
  bool last_line_blank(const cmark_node *node) const;
  void set_last_line_blank(cmark_node *node, bool is_blank);

  // Three-phase line processing
  cmark_node *check_open_blocks(const std::string &input, bool *all_matched);
  void open_new_blocks(cmark_node **container, const std::string &input,
                       bool all_matched);
  void add_text_to_container(cmark_node *container,
                             cmark_node *last_matched_container,
                             const std::string &input);

  // Text accumulation
  void add_line(const std::string &line, size_t start_offset);

  // List parsing
  size_t parse_list_marker(const std::string &input, size_t pos,
                           bool interrupts_paragraph, ListMetadata &data);

  // Thematic break
  size_t scan_thematic_break(const std::string &input, size_t offset);

  // Utility
  char peek_at(const std::string &input, size_t pos) const;
  void chop_trailing_hashtags(std::string &line);
};

#endif // STREAMING_PARSER_H
