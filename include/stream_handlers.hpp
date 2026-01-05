#ifndef STREAM_HANDLERS_H
#define STREAM_HANDLERS_H

#include <optional>
#include <span>
#include <streambuf>
#include <string>

class LineStream {
private:
  std::streambuf &in;
  std::string buffer;
  size_t size = 0;
  size_t pos = 0;
  static constexpr size_t MAX_BUFFER_SIZE = 1024 * 1024;
  static constexpr size_t MAX_LINE_LENGTH = 64 * 1024;

public:
  LineStream(std::streambuf &input) : in(input) {}

  bool receive(std::span<const std::byte> data);

  std::optional<std::string> get_line();

  size_t buffer_size() const { return buffer.size(); }
};

#endif
