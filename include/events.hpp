#ifndef EVENTS_HPP
#define EVENTS_HPP

#include "ast_node.hpp"
#include <cstdint>
#include <string>

// Streaming parser event for incremental rendering
// Uses depth-based tracking instead of IDs for simplicity and speed
struct BlockEvent {
  enum Action : uint8_t {
    Open,   // Block started (html is empty)
    Update, // Block content changed (html contains partial render)
    Close   // Block finalized (html contains final render)
  };

  Action action;
  NodeType type;
  uint8_t depth;       // Nesting level (0 = top-level, 1 = nested once, etc.)
  std::string html;    // Rendered HTML (empty for Open, populated for Update/Close)
};

// Type alias for consistency with existing code
using ParseEvent = BlockEvent;

// Event type helpers
inline bool is_open_event(const BlockEvent &ev) {
  return ev.action == BlockEvent::Open;
}

inline bool is_close_event(const BlockEvent &ev) {
  return ev.action == BlockEvent::Close;
}

inline bool is_update_event(const BlockEvent &ev) {
  return ev.action == BlockEvent::Update;
}

#endif // EVENTS_HPP
