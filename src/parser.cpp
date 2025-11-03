#include <string>

#include "parser.hpp"
#include <cmark.h>

std::string stream_tokens(std::istream &input) {}

cmark_iter *Parser::parse_line(std::string &content) {
  cmark_node *document;
  cmark_parser_feed(parser(), content.c_str(), content.size());
  _iter.reset(cmark_iter_new(current()));
  return iter();
}

cmark_parser *Parser::parser() { return _parser.get(); }
cmark_iter *Parser::iter() { return _iter.get(); }
cmark_node *Parser::current() { return _current.get(); }
