#ifndef HTML_RENDERER_HPP
#define HTML_RENDERER_HPP

#include "ast_node.hpp"
#include "parser.hpp"
#include <string>

// Simple HTML renderer for testing against CommonMark spec
class HtmlRenderer {
public:
  explicit HtmlRenderer(const Parser &parser) : parser_(parser) {}

  std::string render();

private:
  const Parser &parser_;
  std::string output_;

  void render_node(ASTNode::Ptr node);
  void render_children(ASTNode::Ptr node);

  // HTML escaping
  static std::string escape_html(const std::string &text);
};

#endif // HTML_RENDERER_HPP
