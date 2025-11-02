#ifndef PARSER_H
#define PARSER_H

#include <cmark.h>
#include <memory>
#include <string>

class Parser {
private:
  static void ast(cmark_node *n) {
    if (n)
      cmark_node_free(n);
  }
  std::unique_ptr<cmark_node, decltype(&ast)> _root;
  std::string _view;

public:
  Parser(int options = 0) : _root(cmark_node_new(CMARK_NODE_DOCUMENT), &ast), {
    _view.reserve(4096);
  }

  Parser(const Parser &) = delete;
  Parser &operator=(const Parser &) = delete;

  void feed_token(std::string content);

  std::string render();

  // accessors
  cmark_parser *parser();
  cmark_node *root();
};

#endif // PARSER_H
