#ifndef EVENTS_HPP
#define EVENTS_HPP

#include "ast_node.hpp"
#include <cstdint>
#include <string>
#include <variant>

// Unique ID for each block (stable across events)
using BlockId = uint32_t;
constexpr BlockId ROOT_BLOCK_ID = 0;

struct BlockOpenEvent {
  BlockId id;
  BlockId parent_id; // ROOT_BLOCK_ID for top-level blocks
  NodeType type;
};

struct BlockCloseEvent {
  BlockId id;
  BlockId parent_id;
  NodeType type;
  std::string html; // Rendered HTML fragment (empty if emit_html disabled)
};

struct TextUpdateEvent {
  BlockId id;          // The open block receiving text
  std::string content; // Current raw text content
};

using ParseEvent = std::variant<BlockOpenEvent, BlockCloseEvent, TextUpdateEvent>;

// Event type helpers
inline bool is_open_event(const ParseEvent &ev) {
  return std::holds_alternative<BlockOpenEvent>(ev);
}

inline bool is_close_event(const ParseEvent &ev) {
  return std::holds_alternative<BlockCloseEvent>(ev);
}

inline bool is_text_event(const ParseEvent &ev) {
  return std::holds_alternative<TextUpdateEvent>(ev);
}

#endif // EVENTS_HPP
