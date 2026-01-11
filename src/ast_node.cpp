#include "ast_node.hpp"
#include <memory>
ASTNode::Ptr ASTNode::create(NodeType type, int line, int col) {
  return std::make_shared<ASTNode>(ConstructorKey{}, type, line, col);
}

void ASTNode::append_child(ASTNode::Ptr child) {
  child->parent_ = weak_from_this();
  child->prev_ = last_child_;
  if (last_child_) {
    last_child_->next_ = child;
  } else {
    first_child_ = child;
  }
  last_child_ = child;
}

void ASTNode::unlink() {
  if (auto p = prev_.lock())
    p->next_ = next_;
  else if (auto par = parent_.lock())
    par->first_child_ = next_;

  if (next_)
    next_->prev_ = prev_;
  else if (auto par = parent_.lock())
    par->last_child_ = prev_.lock();
  parent_.reset();
  prev_.reset();
  next_.reset();
}

// DFS tree traversal
ASTIterator &ASTIterator::operator++() {
  if (!current_)
    return *this;

  if (auto child = current_->first_child()) {
    current_ = child.get();
    return *this;
  }

  pointer temp = current_;
  while (temp) {
    if (auto next = temp->next()) {
      current_ = next.get();
      return *this;
    }
    auto parent = temp->parent();
    temp = parent ? parent.get() : nullptr;
  }

  current_ = nullptr;
  return *this;
}
