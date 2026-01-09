#include "stream_handlers.hpp"

std::optional<std::string> LineBuffer::get_line() {
  auto linepos = buffer_.find('\n', pos_);
  if (linepos == std::string::npos) {
    return std::nullopt;
  }
  std::string line = buffer_.substr(pos_, linepos - pos_);
  pos_ = linepos + 1;

  if (!line.empty() && line.back() == '\r')
    line.pop_back();

  if (pos_ > 4096) {
    buffer_.erase(0, pos_);
    pos_ = 0;
  }
  return line;
}

bool LineBuffer::grow_buf(std::string_view data) {
  if (buffer_.size() + data.size() > MAX_BUFFER_SIZE)
    return false;

  buffer_.append(data.data(), data.size());
  return true;
}
