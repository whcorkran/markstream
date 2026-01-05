#include "node.hpp"
#include <cstring>

Node::Node(cmark_mem *mem, BlockType type, int start_line, int start_column) {
  node_ = cmark_node_new_with_mem(to_cmark_node_type(type), mem);
  if (node_) {
    node_->flags = CMARK_NODE__OPEN;
    node_->start_line = start_line;
    node_->start_column = start_column;
    node_->end_line = start_line;
  }
}

BlockType Node::get_type() const {
  if (!node_)
    return BlockType::Document;

  return from_cmark_node_type((cmark_node_type)node_->type);
}

void Node::set_type(BlockType type) {
  if (node_) {
    node_->type = (uint16_t)to_cmark_node_type(type);
  }
}

bool Node::is_open() const {
  return node_ && (node_->flags & CMARK_NODE__OPEN) != 0;
}

void Node::set_open(bool open) {
  if (node_) {
    if (open) {
      node_->flags |= CMARK_NODE__OPEN;
    } else {
      node_->flags &= ~CMARK_NODE__OPEN;
    }
  }
}

bool Node::last_line_blank() const {
  return node_ && (node_->flags & CMARK_NODE__LAST_LINE_BLANK) != 0;
}

void Node::set_last_line_blank(bool blank) {
  if (node_) {
    if (blank) {
      node_->flags |= CMARK_NODE__LAST_LINE_BLANK;
    } else {
      node_->flags &= ~CMARK_NODE__LAST_LINE_BLANK;
    }
  }
}

Node Node::get_parent() const { return Node(node_ ? node_->parent : nullptr); }

Node Node::get_first_child() const {
  return Node(node_ ? node_->first_child : nullptr);
}

Node Node::get_last_child() const {
  return Node(node_ ? node_->last_child : nullptr);
}

Node Node::get_next() const { return Node(node_ ? node_->next : nullptr); }

Node Node::get_prev() const { return Node(node_ ? node_->prev : nullptr); }

void Node::set_parent(Node parent) {
  if (node_) {
    node_->parent = parent.get_cmark_node();
  }
}

void Node::set_first_child(Node child) {
  if (node_) {
    node_->first_child = child.get_cmark_node();
  }
}

void Node::set_last_child(Node child) {
  if (node_) {
    node_->last_child = child.get_cmark_node();
  }
}

void Node::set_next(Node next) {
  if (node_) {
    node_->next = next.get_cmark_node();
  }
}

void Node::set_prev(Node prev) {
  if (node_) {
    node_->prev = prev.get_cmark_node();
  }
}

ListMetadata Node::get_list_metadata() const {
  if (!node_)
    return ListMetadata();
  return ListMetadata::from_cmark_list(node_->as.list);
}

void Node::set_list_metadata(const ListMetadata &metadata) {
  if (node_) {
    node_->as.list = metadata.to_cmark_list();
  }
}

Node::CodeMetadata Node::get_code_metadata() const {
  CodeMetadata result;
  if (node_) {
    result.info = node_->as.code.info;
    result.fence_length = node_->as.code.fence_length;
    result.fence_offset = node_->as.code.fence_offset;
    result.fence_char = node_->as.code.fence_char;
    result.fenced = node_->as.code.fenced;
  }
  return result;
}

void Node::set_code_metadata(const CodeMetadata &metadata) {
  if (node_) {
    node_->as.code.info = metadata.info;
    node_->as.code.fence_length = metadata.fence_length;
    node_->as.code.fence_offset = metadata.fence_offset;
    node_->as.code.fence_char = metadata.fence_char;
    node_->as.code.fenced = metadata.fenced;
  }
}

Node::HeadingMetadata Node::get_heading_metadata() const {
  HeadingMetadata result;
  if (node_) {
    result.internal_offset = node_->as.heading.internal_offset;
    result.level = node_->as.heading.level;
    result.setext = node_->as.heading.setext;
  }
  return result;
}

void Node::set_heading_metadata(const HeadingMetadata &metadata) {
  if (node_) {
    node_->as.heading.internal_offset = metadata.internal_offset;
    node_->as.heading.level = metadata.level;
    node_->as.heading.setext = metadata.setext;
  }
}

int Node::get_html_block_type() const {
  return node_ ? node_->as.html_block_type : 0;
}

void Node::set_html_block_type(int type) {
  if (node_) {
    node_->as.html_block_type = type;
  }
}

const char *Node::get_data() const {
  return node_ && node_->data ? reinterpret_cast<const char *>(node_->data)
                              : nullptr;
}

size_t Node::get_data_len() const { return node_ ? node_->len : 0; }

void Node::set_data(const char *data, size_t len) {
  if (!node_)
    return;

  // Free existing data
  if (node_->data && node_->mem) {
    node_->mem->free(node_->data);
  }

  // Allocate and copy new data
  if (data && len > 0 && node_->mem) {
    node_->data = static_cast<unsigned char *>(node_->mem->calloc(1, len + 1));
    if (node_->data) {
      std::memcpy(node_->data, data, len);
      node_->data[len] = '\0';
      node_->len = len;
    }
  } else {
    node_->data = nullptr;
    node_->len = 0;
  }
}
