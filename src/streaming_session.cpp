#include "streaming_session.hpp"
#include "ast_node.hpp"
#include "events.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

void StreamingSession::emit(BlockEvent event) {
  if (callback_) {
    callback_(event);
  } else {
    event_queue_.push(event);
  }
}

bool StreamingSession::accepts_text(NodeType type) const {
  return type == NodeType::Paragraph || type == NodeType::Heading ||
         type == NodeType::CodeBlock || type == NodeType::HtmlBlock;
}

void StreamingSession::process_tree() {
  ASTView tree(parser_.get_root());
  for (auto it = tree.begin(); it != tree.end(); ++it) {
    ASTNode &node = *it;
    if (!node.is_announced()) {
      emit(BlockEvent{
          BlockEvent::Open,
          node.type(),
          &node,
      });
      node.set_announced(true);
    }

    if (node.is_updated()) {
      if (emit_updates_ && node.is_open() && accepts_text(node.type())) {
        emit(BlockEvent{
            BlockEvent::Update,
            node.type(),
            &node,
        });
      }
      node.set_updated(false);
    }

    if (!node.is_open() && !node.is_close_emitted()) {
      emit(BlockEvent{
          BlockEvent::Close,
          node.type(),
          &node,
      });
      node.set_close_emitted(true);
    }
  }
}

void StreamingSession::parse(std::string_view token) {
  if (finished_) {
    throw std::logic_error("StreamingSession::parse called after finish()");
  }

  if (!line_buffer_.feed(token)) {
    throw std::length_error("LineBuffer exceeded maximum size");
  }

  while (auto line = line_buffer_.consume_line()) {
    parser_.parse_line(line.value());
    process_tree();
  }
}

void StreamingSession::finish() {
  if (finished_) {
    return;
  }

  std::string_view remaining = line_buffer_.remaining();
  if (!remaining.empty()) {
    parser_.parse_line(remaining);
    process_tree();
  }

  line_buffer_.clear();
  parser_.finish_document();
  process_tree();
  finished_ = true;
}

void StreamingSession::reset() {
  parser_.reset();
  line_buffer_.clear();

  while (!event_queue_.empty()) {
    event_queue_.pop();
  }

  // Node flags (NODE_ANNOUNCED, NODE_CLOSE_EMITTED) are reset implicitly
  // when parser_.reset() creates fresh nodes.
  finished_ = false;
}

BlockEvent StreamingSession::pop_event() {
  if (event_queue_.empty()) {
    throw std::out_of_range("StreamingSession::pop_event on empty queue");
  }

  BlockEvent event = event_queue_.front();
  event_queue_.pop();
  return event;
}

std::vector<BlockEvent> StreamingSession::pop_events(size_t max_count) {
  std::vector<BlockEvent> events;
  events.reserve(max_count < event_queue_.size() ? max_count
                                                 : event_queue_.size());

  while (max_count > 0 && !event_queue_.empty()) {
    events.push_back(event_queue_.front());
    event_queue_.pop();
    max_count--;
  }

  return events;
}
