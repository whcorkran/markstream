#include <string>

#include "parser.hpp"
#include <cmark.h>

cmark_node *Parser::feed_token(std::string content) {
  cmark_parser *parser = cmark_parser_new_with_mem_into_root(
      options, cmark_get_default_mem_allocator(), _root);
  cmark_node *document;

  S_parser_feed(parser, (const unsigned char *)buffer, len, true);

  document = cmark_parser_finish(parser);
  cmark_parser_free(parser);
  _root = document;
  return document;
}

cmark_node *Parser::root() { return _root; }

cmark_node *Parser::active() { return _active; }

cmark_parser *Parser::parser() { return _parser; }
cmark_iter *Parser::iter() { return _iter; }
