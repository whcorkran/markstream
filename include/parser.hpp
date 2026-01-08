#ifndef PARSER_H
#define PARSER_H

#include "ast_node.hpp"
#include "scanners.hpp"
#include <string>
#include <unordered_map>

class StreamParser {
public:
  explicit StreamParser();
  ~StreamParser() = default;

  void parse_line(const std::string &line);

  ASTNode::Ptr get_root() const { return root_; }
  ASTNode::Ptr get_deepest_open_block() const;
  bool is_complete() const;

  // Access text stored for a node
  const std::string *get_node_text(const ASTNode *node) const {
    auto it = text_storage_.find(node);
    return it != text_storage_.end() ? &it->second : nullptr;
  }

private:
  ASTNode::Ptr root_;
  std::string content_; // Accumulated text for current block
  std::unordered_map<const ASTNode *, std::string> text_storage_;
  int current_line_ = 0;

  // Line processing helpers
  struct FirstNonspace {
    size_t offset;
    size_t column;
    int indent;
    bool blank;
  };

  FirstNonspace find_first_nonspace(const std::string &line, size_t offset,
                                    size_t column) const;
  void advance_offset(const std::string &line, size_t &offset, size_t &column,
                      size_t count, bool columns,
                      bool &partially_consumed_tab) const;

  // Block continuation checkers
  bool parse_block_quote_prefix(const std::string &line, size_t &offset,
                                size_t &column, bool &partially_consumed_tab,
                                const FirstNonspace &fn) const;
  bool parse_list_item_prefix(const std::string &line, ASTNode::Ptr container,
                              size_t &offset, size_t &column,
                              bool &partially_consumed_tab,
                              const FirstNonspace &fn) const;
  bool parse_code_block_prefix(const std::string &line, ASTNode::Ptr container,
                               size_t &offset, size_t &column,
                               bool &partially_consumed_tab,
                               bool *should_continue,
                               const FirstNonspace &fn) const;
  bool parse_html_block_prefix(ASTNode::Ptr container,
                               const FirstNonspace &fn) const;

  // Block creation
  ASTNode::Ptr add_child(ASTNode::Ptr parent, NodeType block_type,
                         int start_column);
  ASTNode::Ptr finalize(ASTNode::Ptr b, size_t last_line_length,
                        const std::string &curline);

  // Block type checks
  bool can_contain(NodeType parent_type, NodeType child_type) const;
  bool accepts_lines(NodeType block_type) const;
  bool last_child_is_open(ASTNode::Ptr container) const;

  // Core algorithm phases
  ASTNode::Ptr check_open_blocks(const std::string &line, bool *all_matched,
                                 size_t &offset, size_t &column,
                                 bool &partially_consumed_tab,
                                 size_t &thematic_break_kill_pos);
  void open_new_blocks(ASTNode::Ptr *container, const std::string &line,
                       bool all_matched, size_t &offset, size_t &column,
                       bool &partially_consumed_tab,
                       size_t &thematic_break_kill_pos);
  void add_text_to_container(ASTNode::Ptr container,
                             ASTNode::Ptr last_matched_container,
                             const std::string &line, size_t &offset,
                             size_t &column, bool &partially_consumed_tab,
                             const FirstNonspace &fn);

  // Text accumulation
  void add_line(const std::string &line, size_t offset, size_t column,
                bool partially_consumed_tab);

  // Utility
  char peek_at(const std::string &input, size_t pos) const;
  void chop_trailing_hashtags(std::string &line) const;
};

#endif // PARSER_H
