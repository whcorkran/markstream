#ifndef PARSER_H
#define PARSER_H

#include <cmark.h>
#include <cstddef>
#include <istream>
#include <memory>
#include <parser.h>
#include <string>
#include <vector>

class Parser {
private:
  static void free_parser(cmark_parser *cp) {
    if (cp)
      cmark_parser_free(cp);
  }

  static void free_node(cmark_node *cn) {
    if (cn)
      cmark_node_free(cn);
  }

  static void free_iter(cmark_iter *ci) {
    if (ci)
      cmark_iter_free(ci);
  }

  std::unique_ptr<cmark_parser, decltype(&free_parser)> _parser;
  std::unique_ptr<cmark_node, decltype(&free_node)> _current;
  std::unique_ptr<cmark_iter, decltype(&free_iter)> _iter;
  std::string _view;

public:
  Parser(int options = 0)
      : _parser(cmark_parser_new(0), &free_parser), _iter(nullptr, &free_iter),
        _current(_parser.get()->root, &free_node) {
    _view.reserve(4096);
  }

  Parser(const Parser &) = delete;
  Parser &operator=(const Parser &) = delete;

  void stream_tokens(std::istream &socket);

  cmark_iter *parse_line(std::string &content);
  std::string render(cmark_iter *tree);

  // accessors
  cmark_parser *parser();
  cmark_iter *iter();
  cmark_node *current();
};

#endif // PARSER_H
