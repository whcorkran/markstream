#include "streaming_session.hpp"
#include "events.hpp"
#include <string>
#include <string_view>

LineBuffer::LineBuffer(size_t max_buffer_size) : pos(0) {}

bool LineBuffer::feed(std::string_view &tok) {
  bool can_add = buffer_.size() + tok.size() < max_buffer_size_;
  if (can_add) {
    this->buffer_.append(tok);
  }
  return false;
}

std::optional<std::string_view> LineBuffer::consume_line() {
  size_t line_end = buffer_.find('\n', pos);
  if (line_end == std::string::npos) {
    return std::nullopt;
  }
  std::string_view line(buffer_.data() + pos, line_end - pos);
  pos = line_end + 1;
  return line;
}

StreamingSession::StreamingSession(EventCallback callback) {}
