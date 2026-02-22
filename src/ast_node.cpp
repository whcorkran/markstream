#include "ast_node.hpp"
#include <memory>

ASTNode::Ptr ASTNode::create(NodeType type, int line, int col) {
  return std::make_shared<ASTNode>(ConstructorKey{}, type, line, col);
}

// DFS tree traversal
ASTIterator &ASTIterator::operator++() {
  while (!nav_stack_.empty()) {
    ChildOf &children = nav_stack_.back();
    nav_stack_.pop_back();
    if (children.child_idx < children.parent->children().size()) {
      const ASTNode::Ptr &child =
          children.parent->children()[children.child_idx++];
      nav_stack_.push_back({child, 0});
    } else {
      current_ = &node;
    }

    current_ = node.get();
  }
}
