#ifndef HTML_RENDERER_HPP
#define HTML_RENDERER_HPP

#include "ast_node.hpp"
#include <string>
#include <string_view>
#include <unordered_map>

struct LinkDef;

// Simple HTML renderer for testing against CommonMark spec
class HtmlRenderer {
public:
  explicit HtmlRenderer() = default;

  std::string &
  render(ASTNode::Ptr root,
         const std::unordered_map<std::string, LinkDef> *link_defs = nullptr);

  std::string_view view() { return std::string_view(output_); }

private:
  std::string output_;
  const std::unordered_map<std::string, LinkDef> *link_defs_ = nullptr;

  void render_node(ASTNode::Ptr node);
  void render_children(ASTNode::Ptr node);
  void render_list_item(ASTNode::Ptr node, const ListData *list);

  // HTML escaping
  static std::string escape_html(const std::string &text);
};

#endif // HTML_RENDERER_HPP
