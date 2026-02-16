#include <html_renderer.hpp>
#include <parser.hpp>
#include <streaming_session.hpp>
#include <unistd.h>

#include <cstdio>
#include <string>

int main() {
  LineBuffer buf;
  Parser parser;

  char raw[4096];
  ssize_t n;
  while ((n = read(STDIN_FILENO, raw, sizeof(raw))) > 0) {
    buf.feed(std::string_view(raw, static_cast<size_t>(n)));

    while (auto line = buf.consume_line()) {
      parser.parse_line(std::string(line.value()));
    }
  }

  // Handle any remaining partial line (no trailing newline)
  std::string_view rem = buf.remaining();
  if (!rem.empty()) {
    parser.parse_line(std::string(rem));
  }

  HtmlRenderer renderer;
  std::string html = renderer.render(parser.get_root());
  fputs(html.c_str(), stdout);

  return 0;
}
