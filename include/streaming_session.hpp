#ifndef STREAMING_SESSION_HPP
#define STREAMING_SESSION_HPP

#include "ast_node.hpp"
#include "events.hpp"
#include "parser.hpp"
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// Callback type for immediate event dispatch
using EventCallback = std::function<void(const BlockEvent &)>;

// Line buffer: accumulates partial tokens into complete lines
// Normalizes line endings and handles incomplete lines
class LineBuffer {
public:
  explicit LineBuffer(size_t max_buffer_size = 1024 * 1024);

  // Feed data into buffer, normalizing \r\n -> \n
  // Returns false if would exceed max_buffer_size
  bool feed(std::string_view &data);

  // Extract and consume next complete line (including \n)
  // Returns nullopt if no complete line available
  std::optional<std::string_view> consume_line();

  // Check if any data is buffered
  bool has_data() const { return !buffer_.empty(); }

  // Get remaining buffer content (for final flush)
  std::string_view remaining() const {
    return std::string_view(buffer_.data() + pos, buffer_.size());
  }

  // Clear all buffered data
  void clear() { buffer_.clear(); }

  size_t size() const { return buffer_.size(); }

private:
  std::string buffer_;
  size_t pos;
  size_t max_buffer_size_;
};

// StreamingSession: event-based streaming markdown parser
// Optimized for rapid rendering of LLM token streams
//
// Two consumption patterns:
//   1. Callback-based: provide callback in constructor for immediate dispatch
//   2. Poll-based: use has_events() / pop_event() for async/WASM contexts
//
// Usage (callback mode):
//   StreamingSession session([](const BlockEvent& ev) {
//     if (ev.action == BlockEvent::Update) {
//       update_ui(ev.html);
//     }
//   });
//   for (const auto& token : llm_stream) {
//     session.feed(token);
//   }
//   session.finish();
//
// Usage (polling mode):
//   StreamingSession session;
//   session.feed(tokens);
//   while (session.has_events()) {
//     auto event = session.pop_event();
//     process(event);
//   }
//
class StreamingSession {
public:
  // Constructor with optional callback
  explicit StreamingSession(EventCallback callback = nullptr);

  ~StreamingSession() = default;

  // Non-copyable (owns parser state)
  StreamingSession(const StreamingSession &) = delete;
  StreamingSession &operator=(const StreamingSession &) = delete;

  // Movable
  StreamingSession(StreamingSession &&) = default;
  StreamingSession &operator=(StreamingSession &&) = default;

  // -------------------------------------------------------------------------
  // Input
  // -------------------------------------------------------------------------

  // Feed a token/chunk of markdown text
  // Emits Open events for new blocks, Update events for content changes
  void feed(std::string_view token);

  // Signal end of input stream
  // Flushes buffer, closes all remaining open blocks, emits Close events
  void finish();

  // Reset session state (for parsing multiple documents)
  void reset();

  // -------------------------------------------------------------------------
  // Polling interface (alternative to callback)
  // -------------------------------------------------------------------------

  // Check if there are queued events
  bool has_events() const { return !event_queue_.empty(); }

  // Pop single event (precondition: has_events())
  BlockEvent pop_event();

  // Pop multiple events (efficient for WASM FFI)
  std::vector<BlockEvent> pop_events(size_t max_count);

  // -------------------------------------------------------------------------
  // Configuration
  // -------------------------------------------------------------------------

  // Set callback (can be changed at runtime)
  void set_callback(EventCallback callback) { callback_ = std::move(callback); }

  // Enable/disable incremental HTML updates (default: true)
  // When enabled, emits Update events with partial HTML as blocks receive
  // content
  void set_emit_updates(bool enabled) { emit_updates_ = enabled; }

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
  std::queue<BlockEvent> event_queue_;

  // Track which nodes have had Open events emitted
  std::unordered_set<const ASTNode *> announced_;

  // Configuration
  bool emit_updates_ = true;

  // State
  bool finished_ = false;

  // -------------------------------------------------------------------------
  // Private helpers
  // -------------------------------------------------------------------------

  // Emit event via callback or queue
  void emit(BlockEvent event);

  // Walk tree and emit Open/Update/Close events
  void process_tree();

  // Calculate nesting depth of a node
  uint8_t depth_of(const ASTNode *node) const;

  // Check if node type accepts text content
  bool accepts_text(NodeType type) const;

  // Render a single node to HTML
  std::string render_node(const ASTNode *node) const;
};

#endif // STREAMING_SESSION_HPP
