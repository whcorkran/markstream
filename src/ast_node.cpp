#include "ast_node.hpp"
#include <cstddef>
#include <memory>

ASTNode::Ptr ASTNode::create(NodeType type, int line, int col) {
  return std::make_shared<ASTNode>(ConstructorKey{}, type, line, col);
}

const std::vector<ASTNode::Ptr> &ASTNode::children() const {
  return children_;
}

// DFS preorder tree traversal using nav_stack_
// Stack tracks (parent, child_idx) pairs. child_idx is the *next* child to
// visit. We descend into children first; when a node's children are exhausted
// we pop back up and continue with the parent's next sibling.
ASTIterator &ASTIterator::operator++() {
  if (!current_) return *this;

  // Try to descend into first child of current node
  const auto &kids = current_->children();
  if (!kids.empty()) {
    // Push current node onto stack so we can resume its siblings later
    nav_stack_.push_back({current_, 0});
    current_ = kids[0].get();
    return *this;
  }

  // No children -- walk up the stack to find the next unvisited sibling
  while (!nav_stack_.empty()) {
    auto &frame = nav_stack_.back();
    frame.child_idx++;
    const auto &siblings = frame.parent->children();
    if (frame.child_idx < siblings.size()) {
      current_ = siblings[frame.child_idx].get();
      return *this;
    }
    // All children of this parent exhausted, pop and continue up
    nav_stack_.pop_back();
  }

  // Stack empty -- traversal complete
  current_ = nullptr;
  return *this;
}
