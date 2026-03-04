#include "html_renderer.hpp"
#include "inline.hpp"
#include "parser.hpp"

namespace {

// Trim trailing whitespace from a string_view (no allocation)
inline std::string_view trim_trailing_ws(std::string_view sv) {
  while (!sv.empty() &&
         (sv.back() == '\n' || sv.back() == ' ' || sv.back() == '\t'))
    sv.remove_suffix(1);
  return sv;
}

// Trim leading whitespace from a string_view (no allocation)
inline std::string_view trim_leading_ws(std::string_view sv) {
  while (!sv.empty() && (sv[0] == ' ' || sv[0] == '\t'))
    sv.remove_prefix(1);
  return sv;
}

} // namespace

std::string HtmlRenderer::escape_html(const std::string &text) {
  std::string result;
  result.reserve(text.size());

  for (char c : text) {
    switch (c) {
    case '&':
      result += "&amp;";
      break;
    case '<':
      result += "&lt;";
      break;
    case '>':
      result += "&gt;";
      break;
    case '"':
      result += "&quot;";
      break;
    default:
      result += c;
    }
  }
  return result;
}

std::string &HtmlRenderer::render(
    ASTNode::Ptr root,
    const std::unordered_map<std::string, LinkDef> *link_defs) {
  output_.clear();
  link_defs_ = link_defs;
  if (root) {
    render_children(root);
  }
  return output_;
}

void HtmlRenderer::render_children(ASTNode::Ptr node) {
  for (const auto &child : node->children()) {
    render_node(child);
  }
}

void HtmlRenderer::render_node(ASTNode::Ptr node) {
  switch (node->type()) {
  case NodeType::Document:
    render_children(node);
    break;

  case NodeType::BlockQuote:
    output_ += "<blockquote>\n";
    render_children(node);
    output_ += "</blockquote>\n";
    break;

  case NodeType::List: {
    const ListData *list = node->get_data<ListData>();
    if (list && list->is_ordered) {
      if (list->start != 1) {
        output_ += "<ol start=\"";
        output_ += std::to_string(list->start);
        output_ += "\">\n";
      } else {
        output_ += "<ol>\n";
      }
    } else {
      output_ += "<ul>\n";
    }

    // Render items, passing list data so they know tight/loose status
    for (const auto &child : node->children()) {
      render_list_item(child, list);
    }

    if (list && list->is_ordered) {
      output_ += "</ol>\n";
    } else {
      output_ += "</ul>\n";
    }
    break;
  }

  case NodeType::Item:
    // Items should be rendered via render_list_item from the List case.
    // If we get here directly (shouldn't happen in well-formed AST),
    // fall back to loose rendering.
    render_list_item(node, nullptr);
    break;

  case NodeType::CodeBlock: {
    const CodeData *code = node->get_data<CodeData>();
    const std::string &text = node->content();

    if (code && !code->info.empty()) {
      // Extract language (first word of info string)
      std::string lang = resolve_escapes_and_entities(code->info);
      size_t space_pos = lang.find(' ');
      if (space_pos != std::string::npos) {
        lang = lang.substr(0, space_pos);
      }
      output_ += "<pre><code class=\"language-";
      output_ += escape_html(lang);
      output_ += "\">";
    } else {
      output_ += "<pre><code>";
    }

    if (!text.empty()) {
      output_ += escape_html(text);
    }
    output_ += "</code></pre>\n";
    break;
  }

  case NodeType::Heading: {
    constexpr const char *h_open[] = {"",     "<h1>", "<h2>", "<h3>",
                                      "<h4>", "<h5>", "<h6>"};
    constexpr const char *h_close[] = {"",       "</h1>\n", "</h2>\n",
                                       "</h3>\n", "</h4>\n", "</h5>\n",
                                       "</h6>\n"};
    const HeadingData *heading = node->get_data<HeadingData>();
    int level = heading ? heading->level : 1;

    output_ += h_open[level];

    const std::string &text = node->content();
    if (!text.empty()) {
      std::string_view sv = trim_leading_ws(trim_trailing_ws(text));
      output_ += render_inlines_html(sv, link_defs_);
    }

    output_ += h_close[level];
    break;
  }

  case NodeType::HtmlBlock: {
    const std::string &text = node->content();
    if (!text.empty()) {
      output_ += text;
    }
    break;
  }

  case NodeType::Paragraph: {
    const std::string &text = node->content();
    if (text.empty())
      break; // Skip paragraphs consumed by link ref defs
    output_ += "<p>";
    output_ += render_inlines_html(trim_trailing_ws(text), link_defs_);
    output_ += "</p>\n";
    break;
  }

  case NodeType::ThematicBreak:
    output_ += "<hr />\n";
    break;
  }
}

void HtmlRenderer::render_list_item(ASTNode::Ptr node, const ListData *list) {
  bool tight = list && list->is_tight;

  output_ += "<li>";

  if (tight) {
    // Tight list: render children inline, stripping <p> tags
    for (size_t ci = 0; ci < node->children().size(); ci++) {
      const auto &child = node->children()[ci];
      if (child->type() == NodeType::Paragraph) {
        // Render paragraph content without <p> tags
        const std::string &text = child->content();
        if (!text.empty()) {
          output_ += render_inlines_html(trim_trailing_ws(text), link_defs_);
        }
      } else {
        output_ += "\n";
        render_node(child);
      }
    }
    output_ += "</li>\n";
  } else {
    // Loose list: render children normally
    output_ += "\n";
    render_children(node);
    output_ += "</li>\n";
  }
}
