#include "streaming_session.hpp"
#include "events.hpp"
#include <string>
#include <string_view>

LineBuffer::LineBuffer(size_t max_buffer_size)
    : pos_(0), max_buffer_size_(max_buffer_size) {}

bool LineBuffer::feed(std::string_view data) {
  if (buffer_.size() + data.size() > max_buffer_size_)
    return false;
  if (pos_ > 0 && pos_ > buffer_.size() / 2) {
    buffer_.erase(0, pos_);
    pos_ = 0;
  }
  buffer_.append(data);
  return true;
}

std::optional<std::string_view> LineBuffer::consume_line() {
  size_t line_end = buffer_.find('\n', pos_);
  if (line_end == std::string::npos)
    return std::nullopt;
  std::string_view line(buffer_.data() + pos_, line_end - pos_);
  pos_ = line_end + 1;
  return line;
}

StreamingSession::StreamingSession(EventCallback callback)
    : parser_(), line_buffer_(), callback_(std::move(callback)) {}
