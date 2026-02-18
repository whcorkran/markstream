#include "streaming_session.hpp"
#include "ast_node.hpp"
#include "events.hpp"
#include <cstdint>
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

void StreamingSession::emit(BlockEvent event) { callback_(event); }

void StreamingSession::diff(ASTNode::Ptr new_tree, ASTNode::Ptr old_tree,
                            uint8_t depth) {
  if (new_tree == old_tree)
    return;

  if (!old_tree && new_tree) {
    emit({BlockEvent::Open, new_tree->type(), depth, new_tree.get()});
  }
}

// void StreamingSession::process_tree() {
//     ASTView tree(parser_.get_root());
//     for (auto node : tree) {
//
//     }
// }

void StreamingSession::parse(std::string_view token) {
  line_buffer_.feed(token);
  std::optional<std::string_view> line = line_buffer_.consume_line();
  if (line) {
    parser_.parse_line(line.value());
    process_tree();
  }
}
