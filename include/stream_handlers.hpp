#ifndef STREAM_HANDLERS_H
#define STREAM_HANDLERS_H

#include <optional>
#include <string>
#include <string_view>

class LineBuffer {
private:
  std::string buffer_;
  size_t pos_ = 0;
  static constexpr size_t MAX_BUFFER_SIZE = 1024 * 1024;
  static constexpr size_t MAX_LINE_LENGTH = 64 * 1024;

public:
  LineBuffer(size_t max_buffer_size = MAX_BUFFER_SIZE,
             size_t max_line_length = MAX_LINE_LENGTH)
      : buffer_() {
    buffer_.reserve(MAX_BUFFER_SIZE);
  }
  bool grow_buf(std::string_view data);
  std::optional<std::string> get_line();
  size_t buffer_size() const { return buffer_.size(); }
};

#endif
