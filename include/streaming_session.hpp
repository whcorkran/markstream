#ifndef STREAMING_SESSION_HPP
#define STREAMING_SESSION_HPP

#include "ast_node.hpp"
#include "events.hpp"
#include "parser.hpp"
#include <functional>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>

// Callback type for immediate event dispatch
using EventCallback = std::function<void(const ParseEvent &)>;

// StreamingSession wraps the Parser to provide an event-based API suitable
// for streaming scenarios (LLM token streams, SSE, etc.)
//
// Two consumption patterns:
//   1. Callback-based: set_callback() for immediate dispatch
//   2. Poll-based: has_events() / pop_event() for async/WASM contexts
//
// Usage:
//   StreamingSession session;
//   session.set_callback([](const ParseEvent& ev) { ... });
//   for (const auto& token : llm_stream) {
//     session.feed(token);
//   }
//   session.finish();
//
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

class StreamingSession {
public:
  StreamingSession() = default;
  ~StreamingSession() = default;

  // Non-copyable (owns parser state)
  StreamingSession(const StreamingSession &) = delete;
  StreamingSession &operator=(const StreamingSession &) = delete;

  // Move OK
  StreamingSession(StreamingSession &&) = default;
  StreamingSession &operator=(StreamingSession &&) = default;

  // -------------------------------------------------------------------------
  // Configuration (call before feeding)
  // -------------------------------------------------------------------------

  // Set callback for immediate event dispatch
  // If set, events are dispatched immediately and not queued
  void set_callback(EventCallback callback);

  // Whether to include rendered HTML in BlockCloseEvents (default: true)
  void set_emit_html(bool enabled);

  // Whether to emit TextUpdateEvents for open blocks (default: false)
  // Can be noisy - emits on every feed() while a text block is open
  void set_emit_text_updates(bool enabled);

  // -------------------------------------------------------------------------
  // Feeding input
  // -------------------------------------------------------------------------

  // Feed a token/chunk of markdown text
  // May trigger events if blocks open or close
  void feed(std::string_view token);

  // Signal end of input stream
  // Closes all remaining open blocks and emits their close events
  void finish();

  // -------------------------------------------------------------------------
  // Polling interface (alternative to callback)
  // -------------------------------------------------------------------------

  // Check if there are queued events
  bool has_events() const;

  // Get and remove the next event from the queue
  // Precondition: has_events() == true
  ParseEvent pop_event();

  // -------------------------------------------------------------------------
  // State inspection
  // -------------------------------------------------------------------------

  // Access the underlying parser (for advanced use)
  const Parser &parser() const { return parser_; }

  // Check if finish() has been called
  bool is_finished() const { return finished_; }

private:
  Parser parser_;
  LineBuffer line_buffer_;

  // Event dispatch
  EventCallback callback_;
  std::queue<ParseEvent> event_queue_;

  // Block ID tracking
  std::unordered_map<const ASTNode *, BlockId> node_to_id_;
  BlockId next_id_ = 1;

  // Track which nodes have had events emitted
  std::set<const ASTNode *> opened_nodes_;
  std::set<const ASTNode *> closed_nodes_;

  // Configuration
  bool emit_html_ = true;
  bool emit_text_updates_ = false;

  // State
  bool finished_ = false;

  // -------------------------------------------------------------------------
  // Private helpers (implement these)
  // -------------------------------------------------------------------------

  // Emit an event (via callback or queue)
  void emit(ParseEvent event);

  // Get or assign a stable BlockId for a node
  BlockId get_or_assign_id(const ASTNode *node);

  // Get parent's BlockId (ROOT_BLOCK_ID if parent is null or is document root)
  BlockId get_parent_id(const ASTNode *node);

  // After feeding a line, iterate the tree using ASTView and emit events for:
  //   - Newly opened blocks (not in opened_nodes_)
  //   - Newly closed blocks (not in closed_nodes_)
  void process_tree_changes();

  // Render a single node to HTML (for close events)
  std::string render_node_html(const ASTNode *node);
};

#endif // STREAMING_SESSION_HPP
