#include <gtest/gtest.h>
#include "ast_node.hpp"

using Ptr = ASTNode::Ptr;

// ============================================================================
// Node Creation Tests
// ============================================================================

TEST(ASTNode, CreateNode) {
  auto node = ASTNode::create(NodeType::Paragraph, 1, 5);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type(), NodeType::Paragraph);
  EXPECT_EQ(node->start_line(), 1);
  EXPECT_EQ(node->start_col(), 5);
}

TEST(ASTNode, CreateWithDefaults) {
  auto node = ASTNode::create(NodeType::Document);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type(), NodeType::Document);
  EXPECT_EQ(node->start_line(), 0);
  EXPECT_EQ(node->start_col(), 0);
}

TEST(ASTNode, CreateAllTypes) {
  EXPECT_NO_THROW(ASTNode::create(NodeType::Document));
  EXPECT_NO_THROW(ASTNode::create(NodeType::BlockQuote));
  EXPECT_NO_THROW(ASTNode::create(NodeType::List));
  EXPECT_NO_THROW(ASTNode::create(NodeType::Item));
  EXPECT_NO_THROW(ASTNode::create(NodeType::CodeBlock));
  EXPECT_NO_THROW(ASTNode::create(NodeType::Heading));
  EXPECT_NO_THROW(ASTNode::create(NodeType::HtmlBlock));
  EXPECT_NO_THROW(ASTNode::create(NodeType::Paragraph));
  EXPECT_NO_THROW(ASTNode::create(NodeType::ThematicBreak));
}

// ============================================================================
// Flag Tests
// ============================================================================

TEST(ASTNode, InitiallyOpen) {
  auto node = ASTNode::create(NodeType::Paragraph);
  EXPECT_TRUE(node->is_open());
}

TEST(ASTNode, SetOpen) {
  auto node = ASTNode::create(NodeType::Paragraph);
  node->set_open(false);
  EXPECT_FALSE(node->is_open());
  node->set_open(true);
  EXPECT_TRUE(node->is_open());
}

TEST(ASTNode, LastLineBlank) {
  auto node = ASTNode::create(NodeType::Paragraph);
  EXPECT_FALSE(node->last_line_blank());

  node->set_last_line_blank(true);
  EXPECT_TRUE(node->last_line_blank());

  node->set_last_line_blank(false);
  EXPECT_FALSE(node->last_line_blank());
}

TEST(ASTNode, FlagsIndependent) {
  auto node = ASTNode::create(NodeType::Paragraph);
  node->set_open(false);
  node->set_last_line_blank(true);

  EXPECT_FALSE(node->is_open());
  EXPECT_TRUE(node->last_line_blank());
}

// ============================================================================
// Position Tests
// ============================================================================

TEST(ASTNode, InitialPosition) {
  auto node = ASTNode::create(NodeType::Paragraph, 10, 20);
  EXPECT_EQ(node->start_line(), 10);
  EXPECT_EQ(node->start_col(), 20);
  EXPECT_EQ(node->end_line(), 0);
  EXPECT_EQ(node->end_col(), 0);
}

TEST(ASTNode, SetPosition) {
  auto node = ASTNode::create(NodeType::Paragraph);
  node->set_start(5, 10);
  node->set_end(8, 15);

  EXPECT_EQ(node->start_line(), 5);
  EXPECT_EQ(node->start_col(), 10);
  EXPECT_EQ(node->end_line(), 8);
  EXPECT_EQ(node->end_col(), 15);
}

// ============================================================================
// Tree Structure Tests - Basic Navigation
// ============================================================================

TEST(ASTNode, InitiallyNoFamily) {
  auto node = ASTNode::create(NodeType::Paragraph);
  EXPECT_EQ(node->parent(), nullptr);
  EXPECT_EQ(node->first_child(), nullptr);
  EXPECT_EQ(node->last_child(), nullptr);
  EXPECT_EQ(node->next(), nullptr);
  EXPECT_EQ(node->prev(), nullptr);
}

TEST(ASTNode, AppendSingleChild) {
  auto parent = ASTNode::create(NodeType::Document);
  auto child = ASTNode::create(NodeType::Paragraph);

  parent->append_child(child);

  EXPECT_EQ(parent->first_child(), child);
  EXPECT_EQ(parent->last_child(), child);
  EXPECT_EQ(child->parent(), parent);
  EXPECT_EQ(child->next(), nullptr);
  EXPECT_EQ(child->prev(), nullptr);
}

TEST(ASTNode, AppendMultipleChildren) {
  auto parent = ASTNode::create(NodeType::Document);
  auto child1 = ASTNode::create(NodeType::Paragraph);
  auto child2 = ASTNode::create(NodeType::Heading);
  auto child3 = ASTNode::create(NodeType::CodeBlock);

  parent->append_child(child1);
  parent->append_child(child2);
  parent->append_child(child3);

  // Check parent pointers
  EXPECT_EQ(parent->first_child(), child1);
  EXPECT_EQ(parent->last_child(), child3);

  // Check child1
  EXPECT_EQ(child1->parent(), parent);
  EXPECT_EQ(child1->prev(), nullptr);
  EXPECT_EQ(child1->next(), child2);

  // Check child2
  EXPECT_EQ(child2->parent(), parent);
  EXPECT_EQ(child2->prev(), child1);
  EXPECT_EQ(child2->next(), child3);

  // Check child3
  EXPECT_EQ(child3->parent(), parent);
  EXPECT_EQ(child3->prev(), child2);
  EXPECT_EQ(child3->next(), nullptr);
}

TEST(ASTNode, NestedStructure) {
  auto doc = ASTNode::create(NodeType::Document);
  auto quote = ASTNode::create(NodeType::BlockQuote);
  auto para = ASTNode::create(NodeType::Paragraph);

  doc->append_child(quote);
  quote->append_child(para);

  EXPECT_EQ(doc->first_child(), quote);
  EXPECT_EQ(quote->parent(), doc);
  EXPECT_EQ(quote->first_child(), para);
  EXPECT_EQ(para->parent(), quote);
}

// ============================================================================
// Unlink Tests
// ============================================================================

TEST(ASTNode, UnlinkOnlyChild) {
  auto parent = ASTNode::create(NodeType::Document);
  auto child = ASTNode::create(NodeType::Paragraph);

  parent->append_child(child);
  child->unlink();

  // Parent should have no children
  EXPECT_EQ(parent->first_child(), nullptr);
  EXPECT_EQ(parent->last_child(), nullptr);

  // Child should have no family
  EXPECT_EQ(child->parent(), nullptr);
  EXPECT_EQ(child->prev(), nullptr);
  EXPECT_EQ(child->next(), nullptr);
}

TEST(ASTNode, UnlinkFirstChild) {
  auto parent = ASTNode::create(NodeType::Document);
  auto child1 = ASTNode::create(NodeType::Paragraph);
  auto child2 = ASTNode::create(NodeType::Heading);
  auto child3 = ASTNode::create(NodeType::CodeBlock);

  parent->append_child(child1);
  parent->append_child(child2);
  parent->append_child(child3);

  child1->unlink();

  // Parent's first child should be child2
  EXPECT_EQ(parent->first_child(), child2);
  EXPECT_EQ(parent->last_child(), child3);

  // child2 should have no prev
  EXPECT_EQ(child2->prev(), nullptr);
  EXPECT_EQ(child2->next(), child3);

  // child1 should be orphaned
  EXPECT_EQ(child1->parent(), nullptr);
  EXPECT_EQ(child1->next(), nullptr);
}

TEST(ASTNode, UnlinkMiddleChild) {
  auto parent = ASTNode::create(NodeType::Document);
  auto child1 = ASTNode::create(NodeType::Paragraph);
  auto child2 = ASTNode::create(NodeType::Heading);
  auto child3 = ASTNode::create(NodeType::CodeBlock);

  parent->append_child(child1);
  parent->append_child(child2);
  parent->append_child(child3);

  child2->unlink();

  // Parent's children should skip child2
  EXPECT_EQ(parent->first_child(), child1);
  EXPECT_EQ(parent->last_child(), child3);

  // child1 and child3 should link directly
  EXPECT_EQ(child1->next(), child3);
  EXPECT_EQ(child3->prev(), child1);

  // child2 should be orphaned
  EXPECT_EQ(child2->parent(), nullptr);
  EXPECT_EQ(child2->prev(), nullptr);
  EXPECT_EQ(child2->next(), nullptr);
}

TEST(ASTNode, UnlinkLastChild) {
  auto parent = ASTNode::create(NodeType::Document);
  auto child1 = ASTNode::create(NodeType::Paragraph);
  auto child2 = ASTNode::create(NodeType::Heading);
  auto child3 = ASTNode::create(NodeType::CodeBlock);

  parent->append_child(child1);
  parent->append_child(child2);
  parent->append_child(child3);

  child3->unlink();

  // Parent's last child should be child2
  EXPECT_EQ(parent->first_child(), child1);
  EXPECT_EQ(parent->last_child(), child2);

  // child2 should have no next
  EXPECT_EQ(child2->next(), nullptr);

  // child3 should be orphaned
  EXPECT_EQ(child3->parent(), nullptr);
  EXPECT_EQ(child3->prev(), nullptr);
}

TEST(ASTNode, UnlinkDoesNotDelete) {
  auto parent = ASTNode::create(NodeType::Document);
  auto child = ASTNode::create(NodeType::Paragraph);

  parent->append_child(child);
  auto weak_child = std::weak_ptr<ASTNode>(child);

  child->unlink();

  // child still exists (we hold a shared_ptr)
  EXPECT_FALSE(weak_child.expired());
  EXPECT_NE(child, nullptr);
  EXPECT_EQ(child->type(), NodeType::Paragraph);
}

TEST(ASTNode, UnlinkAndReattach) {
  auto parent1 = ASTNode::create(NodeType::Document);
  auto parent2 = ASTNode::create(NodeType::BlockQuote);
  auto child = ASTNode::create(NodeType::Paragraph);

  parent1->append_child(child);
  child->unlink();
  parent2->append_child(child);

  EXPECT_EQ(parent1->first_child(), nullptr);
  EXPECT_EQ(parent2->first_child(), child);
  EXPECT_EQ(child->parent(), parent2);
}

// ============================================================================
// Metadata Tests
// ============================================================================

TEST(ASTNode, MetadataInitiallyEmpty) {
  auto node = ASTNode::create(NodeType::Paragraph);
  EXPECT_EQ(node->get_data<ListData>(), nullptr);
  EXPECT_EQ(node->get_data<CodeData>(), nullptr);
  EXPECT_EQ(node->get_data<HeadingData>(), nullptr);
  EXPECT_EQ(node->get_data<int>(), nullptr);
}

TEST(ASTNode, SetListData) {
  auto node = ASTNode::create(NodeType::List);
  ListData list{.start = 1, .padding = 2, .marker_char = '-', .is_tight = true};

  node->set_data(list);

  auto* retrieved = node->get_data<ListData>();
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->start, 1);
  EXPECT_EQ(retrieved->padding, 2);
  EXPECT_EQ(retrieved->marker_char, '-');
  EXPECT_TRUE(retrieved->is_tight);
}

TEST(ASTNode, SetCodeData) {
  auto node = ASTNode::create(NodeType::CodeBlock);
  CodeData code{.info = "python", .fence_length = 3, .fence_char = '`'};

  node->set_data(code);

  auto* retrieved = node->get_data<CodeData>();
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->info, "python");
  EXPECT_EQ(retrieved->fence_length, 3);
  EXPECT_EQ(retrieved->fence_char, '`');
}

TEST(ASTNode, SetHeadingData) {
  auto node = ASTNode::create(NodeType::Heading);
  HeadingData heading{.level = 2, .setext = false};

  node->set_data(heading);

  auto* retrieved = node->get_data<HeadingData>();
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->level, 2);
  EXPECT_FALSE(retrieved->setext);
}

TEST(ASTNode, SetHtmlBlockType) {
  auto node = ASTNode::create(NodeType::HtmlBlock);
  node->set_data(3); // Type 3 processing instruction

  auto* retrieved = node->get_data<int>();
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(*retrieved, 3);
}

TEST(ASTNode, MetadataReplacement) {
  auto node = ASTNode::create(NodeType::Heading);

  HeadingData h1{.level = 1, .setext = false};
  node->set_data(h1);

  EXPECT_EQ(node->get_data<HeadingData>()->level, 1);

  HeadingData h2{.level = 3, .setext = true};
  node->set_data(h2);

  EXPECT_EQ(node->get_data<HeadingData>()->level, 3);
  EXPECT_TRUE(node->get_data<HeadingData>()->setext);
}

TEST(ASTNode, WrongMetadataTypeReturnsNull) {
  auto node = ASTNode::create(NodeType::Heading);
  HeadingData heading{.level = 2, .setext = false};
  node->set_data(heading);

  // Try to get wrong type
  EXPECT_EQ(node->get_data<ListData>(), nullptr);
  EXPECT_EQ(node->get_data<CodeData>(), nullptr);
  EXPECT_EQ(node->get_data<int>(), nullptr);

  // But correct type works
  EXPECT_NE(node->get_data<HeadingData>(), nullptr);
}

// ============================================================================
// Memory Management Tests
// ============================================================================

TEST(ASTNode, ChildrenKeptAliveByParent) {
  auto parent = ASTNode::create(NodeType::Document);
  std::weak_ptr<ASTNode> weak_child;

  {
    auto child = ASTNode::create(NodeType::Paragraph);
    weak_child = child;
    parent->append_child(child);
    // child goes out of scope here
  }

  // Child should still be alive (held by parent)
  EXPECT_FALSE(weak_child.expired());
  EXPECT_NE(parent->first_child(), nullptr);
}

TEST(ASTNode, ParentDoesNotKeepChildrenAfterDestruction) {
  std::weak_ptr<ASTNode> weak_child;

  {
    auto parent = ASTNode::create(NodeType::Document);
    auto child = ASTNode::create(NodeType::Paragraph);
    weak_child = child;
    parent->append_child(child);
    // parent and child go out of scope
  }

  // Both should be destroyed
  EXPECT_TRUE(weak_child.expired());
}

TEST(ASTNode, UnlinkedChildNeedsExternalReference) {
  auto parent = ASTNode::create(NodeType::Document);
  std::weak_ptr<ASTNode> weak_child;

  {
    auto child = ASTNode::create(NodeType::Paragraph);
    weak_child = child;
    parent->append_child(child);
    child->unlink();
    // child goes out of scope here
  }

  // Child was unlinked and no other references exist
  EXPECT_TRUE(weak_child.expired());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(ASTNode, AppendChildToItself) {
  auto node = ASTNode::create(NodeType::Document);
  // This would create a cycle - undefined behavior but shouldn't crash
  // In practice, parser logic should prevent this
  EXPECT_NO_THROW(node->append_child(node));
}

TEST(ASTNode, UnlinkNodeWithoutParent) {
  auto node = ASTNode::create(NodeType::Paragraph);
  EXPECT_NO_THROW(node->unlink());
  EXPECT_EQ(node->parent(), nullptr);
}

TEST(ASTNode, DeepHierarchy) {
  auto root = ASTNode::create(NodeType::Document);
  auto current = root;

  // Create 100-level deep hierarchy
  for (int i = 0; i < 100; i++) {
    auto child = ASTNode::create(NodeType::BlockQuote);
    current->append_child(child);
    current = child;
  }

  // Navigate back up
  int levels = 0;
  current = current->parent();
  while (current) {
    levels++;
    current = current->parent();
  }

  EXPECT_EQ(levels, 100);
}
