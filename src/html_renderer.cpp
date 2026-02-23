#include "html_renderer.hpp"

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

std::string HtmlRenderer::render(ASTNode::Ptr root) {
  output_.clear();
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
        output_ += "<ol start=\"" + std::to_string(list->start) + "\">\n";
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
      std::string lang = code->info;
      size_t space_pos = lang.find(' ');
      if (space_pos != std::string::npos) {
        lang = lang.substr(0, space_pos);
      }
      output_ += "<pre><code class=\"language-" + escape_html(lang) + "\">";
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
    const HeadingData *heading = node->get_data<HeadingData>();
    int level = heading ? heading->level : 1;
    std::string tag = "h" + std::to_string(level);

    output_ += "<" + tag + ">";

    const std::string &text = node->content();
    if (!text.empty()) {
      std::string content = text;
      // Trim trailing newline
      while (!content.empty() && content.back() == '\n') {
        content.pop_back();
      }
      // Trim leading whitespace
      size_t start = 0;
      while (start < content.size() &&
             (content[start] == ' ' || content[start] == '\t')) {
        start++;
      }
      if (start > 0) {
        content = content.substr(start);
      }
      output_ += escape_html(content);
    }

    output_ += "</" + tag + ">\n";
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
    output_ += "<p>";
    const std::string &text = node->content();
    if (!text.empty()) {
      std::string content = text;
      // Trim trailing newline
      while (!content.empty() && content.back() == '\n') {
        content.pop_back();
      }
      output_ += escape_html(content);
    }
    output_ += "</p>\n";
    break;
  }

  case NodeType::ThematicBreak:
    output_ += "<hr />\n";
    break;
  }
}

void HtmlRenderer::render_list_item(ASTNode::Ptr node,
                                    const ListData *list) {
  bool tight = list && list->is_tight;

  output_ += "<li>";

  if (tight) {
    // Tight list: render children inline, stripping <p> tags
    for (const auto &child : node->children()) {
      if (child->type() == NodeType::Paragraph) {
        // Render paragraph content without <p> tags
        const std::string &text = child->content();
        if (!text.empty()) {
          std::string content = text;
          // Trim trailing newline
          while (!content.empty() && content.back() == '\n') {
            content.pop_back();
          }
          output_ += escape_html(content);
        }
      } else {
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
