#ifndef SCANNERS
#define SCANNERS

#include <array>
#include <cstdint>
#include <string>

enum class BlockType : uint8_t {
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

struct BlockSpec {
  BlockType type;

  // Delimiters
  char delim_char = '\0';
  char alternate_delims[16] = {'\0'};
  size_t min_repeat = 1;
  size_t max_repeat = 1;

  // Structure
  bool is_container = false;
  bool is_leaf = true;

  // Behavior
  bool accepts_lazy_continuation = false;
  bool requires_continuation_marker = false;
  bool is_single_line = false;
  bool autoclose_on_parent_close = false;
};

// Using designated initializers - only non-default values shown
inline constexpr std::array<BlockSpec, 9> BlockSpecs{{

    {.type = BlockType::Document, .is_container = true, .is_leaf = false},

    // BlockQuote
    {.type = BlockType::BlockQuote,
     .delim_char = '>',
     .is_container = true,
     .is_leaf = false,
     .requires_continuation_marker = true},

    // List
    {.type = BlockType::List, .is_container = true, .is_leaf = false},

    // Item
    {.type = BlockType::Item,
     .delim_char = '-',
     .is_container = true,
     .is_leaf = false},

    // CodeBlock
    {.type = BlockType::CodeBlock,
     .delim_char = '`',
     .alternate_delims = {'`', '~', '\0'},
     .min_repeat = 3,
     .max_repeat = 0,
     .autoclose_on_parent_close = true},

    // Heading - ATX ONLY
    {.type = BlockType::Heading,
     .delim_char = '#',
     .min_repeat = 1,
     .max_repeat = 6,
     .is_single_line = true},

    // HtmlBlock
    {.type = BlockType::HtmlBlock, .delim_char = '<', .min_repeat = 1},

    // Paragraph
    {.type = BlockType::Paragraph, .accepts_lazy_continuation = true},

    // ThematicBreak
    {.type = BlockType::ThematicBreak,
     .delim_char = '*',
     .alternate_delims = {'*', '-', '_', '\0'},
     .min_repeat = 3,
     .max_repeat = 0,
     .is_single_line = true},
}};

namespace validation {
inline bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

inline bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool is_punct(char c) { return (c >= '!' && c <= '/'); }

inline constexpr const BlockSpec get_spec(BlockType block) {
  return BlockSpecs[static_cast<size_t>(block)];
}

} // namespace validation

// Generic delimiter scanner (handles primary + alternates)
size_t scan_block_delimiter(const std::string &line, size_t offset,
                            BlockType block, char *out_delim_char);

// Block-specific scanners
size_t scan_atx_heading_start(const std::string &line, size_t offset);
size_t scan_thematic_break(const std::string &line, size_t offset,
                           char *out_delim_char);

// HTML blocks
size_t scan_html_block_start(const std::string &line, size_t offset);
size_t scan_html_block_start_7(const std::string &line, size_t offset);
size_t scan_html_block_end_N(const std::string &line, size_t offset);

#endif // SCANNERS
