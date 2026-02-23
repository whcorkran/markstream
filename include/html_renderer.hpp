#ifndef HTML_RENDERER_HPP
#define HTML_RENDERER_HPP

#include "ast_node.hpp"
#include <string>

// Simple HTML renderer for testing against CommonMark spec
class HtmlRenderer {
public:
  explicit HtmlRenderer() = default;

  std::string render(ASTNode::Ptr root);

private:
  std::string output_;

  void render_node(ASTNode::Ptr node);
  void render_children(ASTNode::Ptr node);
  void render_list_item(ASTNode::Ptr node, const ListData *list);

  // HTML escaping
  static std::string escape_html(const std::string &text);
};

#endif // HTML_RENDERER_HPP
