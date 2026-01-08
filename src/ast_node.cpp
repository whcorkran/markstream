#include "ast_node.hpp"
ASTNode::Ptr ASTNode::create(NodeType type, int line, int col) {
  return ASTNode::Ptr(new ASTNode(type, line, col));
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
