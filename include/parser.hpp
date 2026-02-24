#ifndef PARSER_H
#define PARSER_H

#include "ast_node.hpp"
#include <string>
#include <string_view>
#include <vector>

class Parser {
public:
  explicit Parser();
  ~Parser() = default;

  void parse_line(std::string_view line);
  void finish_document();
  void reset();

  ASTNode::Ptr get_root() const { return root_; }
  ASTNode::Ptr get_deepest_open_block() const;
  bool is_complete() const;

  // Access the open blocks stack (for StreamingSession depth queries)
  const std::vector<ASTNode::Ptr> &open_blocks() const {
    return open_blocks_;
  }

private:
  ASTNode::Ptr root_;
  int current_line_ = 0;

  // Explicit stack of open blocks: open_blocks_[0] is root,
  // open_blocks_.back() is the deepest open block.
  // Replaces the old parent-pointer walking.
  std::vector<ASTNode::Ptr> open_blocks_;

  // Stack depth saved before phase 2 runs, so phase 3 knows which blocks
  // were pre-existing (and thus eligible for finalization) vs newly created.
  size_t pre_phase2_depth_ = 0;

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

  // Block continuation checkers (phase 1)
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

  // Block creation and finalization using open_blocks_ stack
  ASTNode::Ptr add_child(ASTNode::Ptr parent, NodeType block_type,
                         int start_column);
  void finalize(ASTNode::Ptr b);
  void finalize_above(size_t target_depth);

  // Block type checks
  bool can_contain(NodeType parent_type, NodeType child_type) const;
  bool accepts_lines(NodeType block_type) const;
  bool last_child_is_open(ASTNode::Ptr container) const;

  // Phase 2 context: bundles mutable state threaded through try_* functions
  struct OpenBlockCtx {
    ASTNode::Ptr &container;
    const std::string &line;
    size_t &offset;
    size_t &column;
    bool &partially_consumed_tab;
    const FirstNonspace &fn;
    bool indented;
    bool maybe_lazy;
    bool all_matched;
  };

  // try_* return values for phase 2
  enum class BlockStart {
    None,  // did not match -- try next starter
    Found, // matched, container may accept more blocks (continue loop)
    Leaf,  // matched, container accepts lines (break loop)
  };

  // Phase 2: new block starters (priority order)
  BlockStart try_block_quote(OpenBlockCtx &ctx);
  BlockStart try_atx_heading(OpenBlockCtx &ctx);
  BlockStart try_code_fence(OpenBlockCtx &ctx);
  BlockStart try_html_block(OpenBlockCtx &ctx);
  BlockStart try_setext_heading(OpenBlockCtx &ctx);
  BlockStart try_thematic_break(OpenBlockCtx &ctx);
  BlockStart try_list_item(OpenBlockCtx &ctx);
  BlockStart try_indented_code(OpenBlockCtx &ctx);

  // Core algorithm phases
  ASTNode::Ptr check_open_blocks(const std::string &line, bool *all_matched,
                                 size_t &offset, size_t &column,
                                 bool &partially_consumed_tab);
  void open_new_blocks(ASTNode::Ptr *container, const std::string &line,
                       bool all_matched, size_t &offset, size_t &column,
                       bool &partially_consumed_tab);
  void add_text_to_container(ASTNode::Ptr container,
                             ASTNode::Ptr last_matched_container,
                             ASTNode::Ptr deepest_before_new,
                             const std::string &line, size_t &offset,
                             size_t &column, bool &partially_consumed_tab,
                             const FirstNonspace &fn);

  // Text accumulation
  void add_line(ASTNode::Ptr target, const std::string &line, size_t offset,
                size_t column, bool partially_consumed_tab);

  // Utility
  char peek_at(const std::string &input, size_t pos) const;
  void chop_trailing_hashtags(std::string &line) const;
};

#endif // PARSER_H
