#ifndef SCANNERS
#define SCANNERS

#include <string>

namespace char_validation {
inline bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

inline bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

} // namespace char_validation

// Various scanners for blocks start and end
size_t scan_atx_heading_start(std::string line, size_t offset);
// size_t scan_setext_heading_start(std::string line, size_t offset);
// may implement later
//
size_t scan_open_code_fence(std::string line, size_t offset);
size_t scan_close_code_fence(std::string line, size_t offset);
size_t scan_html_block_start(std::string line, size_t offset);
size_t scan_html_block_start_7(std::string line, size_t offset);
size_t scan_html_block_end_N(std::string line, size_t offset);

#endif // SCANNERS
