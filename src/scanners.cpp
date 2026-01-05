#include "scanners.hpp"

using namespace char_validation;

size_t scan_atx_heading_start(const std::string line, size_t offset) {
  size_t pos = offset;
  size_t count = 0;

  while (pos < line.size() && line[pos] == '#' && count < 6) {
    count++;
    pos++;
  }

  if (count == 0)
    return 0;

  if (pos >= line.size() || is_space(line[pos])) {
    return count;
  }
  return 0;
}

size_t scan_open_code_fence(std::string line, size_t offset);
size_t scan_close_code_fence(std::string line, size_t offset);
size_t scan_html_block_start(std::string line, size_t offset);
size_t scan_html_block_start_7(std::string line, size_t offset);
size_t scan_html_block_end_N(std::string line, size_t offset);
