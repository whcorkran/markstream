#include "ast_node.hpp"
#include <cstddef>
#include <memory>

ASTNode::Ptr ASTNode::create(NodeType type, int line, int col) {
  return std::make_shared<ASTNode>(ConstructorKey{}, type, line, col);
}

// DFS preorder tree traversal
ASTIterator &ASTIterator::operator++() {
  if (!nav_stack_.empty()) {
    auto &children = nav_stack_.back();
    auto *node = children.parent;

    if (children.child_idx < node->children().size()) {
      pointer child = node->children()[children.child_idx++].get();
      nav_stack_.emplace_back(child, 0);
    } else {
      current_ = node;
      nav_stack_.pop_back();
      return *this;
    }
  }
  current_ = nullptr;
  return *this;
}
