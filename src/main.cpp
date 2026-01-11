#include <ast_node.hpp>
#include <parser.hpp>
#include <streaming_session.hpp>
#include <unistd.h>

int main() {
  LineBuffer stream;
  Parser parser;

  char buf[128];
  ssize_t n;
  while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
    stream.grow_buf(std::string_view{buf, size_t(n)});

    while (auto line = stream.get_line()) {
      parser.parse_line(line.value());
    }
  }

  return 0;
}
