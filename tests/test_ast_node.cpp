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
// Tree Structure Tests - Vector Children API
// ============================================================================

TEST(ASTNode, InitiallyNoChildren) {
  auto node = ASTNode::create(NodeType::Paragraph);
  EXPECT_EQ(node->first_child(), nullptr);
  EXPECT_EQ(node->last_child(), nullptr);
  EXPECT_TRUE(node->children().empty());
}

TEST(ASTNode, AddSingleChild) {
  auto parent = ASTNode::create(NodeType::Document);
  auto child = ASTNode::create(NodeType::Paragraph);

  parent->add_child(child);

  EXPECT_EQ(parent->children().size(), 1u);
  EXPECT_EQ(parent->first_child(), child);
  EXPECT_EQ(parent->last_child(), child);
  EXPECT_EQ(parent->children()[0], child);
}

TEST(ASTNode, AddMultipleChildren) {
  auto parent = ASTNode::create(NodeType::Document);
  auto child1 = ASTNode::create(NodeType::Paragraph);
  auto child2 = ASTNode::create(NodeType::Heading);
  auto child3 = ASTNode::create(NodeType::CodeBlock);

  parent->add_child(child1);
  parent->add_child(child2);
  parent->add_child(child3);

  EXPECT_EQ(parent->children().size(), 3u);
  EXPECT_EQ(parent->first_child(), child1);
  EXPECT_EQ(parent->last_child(), child3);
  EXPECT_EQ(parent->children()[0], child1);
  EXPECT_EQ(parent->children()[1], child2);
  EXPECT_EQ(parent->children()[2], child3);
}

TEST(ASTNode, NestedStructure) {
  auto doc = ASTNode::create(NodeType::Document);
  auto quote = ASTNode::create(NodeType::BlockQuote);
  auto para = ASTNode::create(NodeType::Paragraph);

  doc->add_child(quote);
  quote->add_child(para);

  EXPECT_EQ(doc->first_child(), quote);
  EXPECT_EQ(quote->first_child(), para);
  EXPECT_EQ(doc->children().size(), 1u);
  EXPECT_EQ(quote->children().size(), 1u);
  EXPECT_TRUE(para->children().empty());
}

TEST(ASTNode, ReplaceLastChild) {
  auto parent = ASTNode::create(NodeType::Document);
  auto child1 = ASTNode::create(NodeType::Paragraph);
  auto child2 = ASTNode::create(NodeType::Heading);
  auto replacement = ASTNode::create(NodeType::CodeBlock);

  parent->add_child(child1);
  parent->add_child(child2);
  parent->replace_last_child(replacement);

  EXPECT_EQ(parent->children().size(), 2u);
  EXPECT_EQ(parent->first_child(), child1);
  EXPECT_EQ(parent->last_child(), replacement);
  EXPECT_EQ(parent->children()[0], child1);
  EXPECT_EQ(parent->children()[1], replacement);
}

TEST(ASTNode, ChildrenOrderPreserved) {
  auto parent = ASTNode::create(NodeType::Document);

  for (int i = 0; i < 10; i++) {
    parent->add_child(ASTNode::create(NodeType::Paragraph, i, 0));
  }

  EXPECT_EQ(parent->children().size(), 10u);
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(parent->children()[i]->start_line(), i);
  }
}

// ============================================================================
// Content Tests
// ============================================================================

TEST(ASTNode, ContentInitiallyEmpty) {
  auto node = ASTNode::create(NodeType::Paragraph);
  EXPECT_TRUE(node->content().empty());
}

TEST(ASTNode, AppendContent) {
  auto node = ASTNode::create(NodeType::Paragraph);
  node->append_content("hello ");
  node->append_content("world");
  EXPECT_EQ(node->content(), "hello world");
}

TEST(ASTNode, SetContent) {
  auto node = ASTNode::create(NodeType::Paragraph);
  node->append_content("old");
  std::string new_content = "new content";
  node->set_content(std::move(new_content));
  EXPECT_EQ(node->content(), "new content");
}

TEST(ASTNode, ClearContent) {
  auto node = ASTNode::create(NodeType::Paragraph);
  node->append_content("some text");
  node->clear_content();
  EXPECT_TRUE(node->content().empty());
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

  auto *retrieved = node->get_data<ListData>();
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

  auto *retrieved = node->get_data<CodeData>();
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->info, "python");
  EXPECT_EQ(retrieved->fence_length, 3);
  EXPECT_EQ(retrieved->fence_char, '`');
}

TEST(ASTNode, SetHeadingData) {
  auto node = ASTNode::create(NodeType::Heading);
  HeadingData heading{.level = 2, .setext = false};

  node->set_data(heading);

  auto *retrieved = node->get_data<HeadingData>();
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->level, 2);
  EXPECT_FALSE(retrieved->setext);
}

TEST(ASTNode, SetHtmlBlockType) {
  auto node = ASTNode::create(NodeType::HtmlBlock);
  node->set_data(3); // Type 3 processing instruction

  auto *retrieved = node->get_data<int>();
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
    parent->add_child(child);
    // child local var goes out of scope here
  }

  // Child should still be alive (held by parent's children vector)
  EXPECT_FALSE(weak_child.expired());
  EXPECT_NE(parent->first_child(), nullptr);
}

TEST(ASTNode, ParentDoesNotKeepChildrenAfterDestruction) {
  std::weak_ptr<ASTNode> weak_child;

  {
    auto parent = ASTNode::create(NodeType::Document);
    auto child = ASTNode::create(NodeType::Paragraph);
    weak_child = child;
    parent->add_child(child);
    // parent and child go out of scope
  }

  // Both should be destroyed
  EXPECT_TRUE(weak_child.expired());
}

TEST(ASTNode, ReplaceLastChildReleasesOld) {
  auto parent = ASTNode::create(NodeType::Document);
  std::weak_ptr<ASTNode> weak_old;

  {
    auto old_child = ASTNode::create(NodeType::Paragraph);
    weak_old = old_child;
    parent->add_child(old_child);
    // old_child local var goes out of scope
  }

  // Still alive -- held by parent
  EXPECT_FALSE(weak_old.expired());

  // Replace with a new child
  auto new_child = ASTNode::create(NodeType::Heading);
  parent->replace_last_child(new_child);

  // Old child no longer referenced by parent, should be destroyed
  EXPECT_TRUE(weak_old.expired());
  EXPECT_EQ(parent->last_child(), new_child);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(ASTNode, DeepHierarchy) {
  auto root = ASTNode::create(NodeType::Document);
  auto current = root;

  // Create 100-level deep hierarchy
  for (int i = 0; i < 100; i++) {
    auto child = ASTNode::create(NodeType::BlockQuote);
    current->add_child(child);
    current = child;
  }

  // Walk down last_child chain to count depth
  int depth = 0;
  current = root;
  while (current->last_child()) {
    current = current->last_child();
    depth++;
  }

  EXPECT_EQ(depth, 100);
}

TEST(ASTNode, EmptyChildrenIsStable) {
  auto node = ASTNode::create(NodeType::Document);

  // Multiple accesses to empty children should be stable
  EXPECT_TRUE(node->children().empty());
  EXPECT_EQ(node->children().size(), 0u);
  EXPECT_EQ(node->first_child(), nullptr);
  EXPECT_EQ(node->last_child(), nullptr);
}

// ============================================================================
// ASTIterator Tests
// ============================================================================

TEST(ASTIterator, SingleNode) {
  auto root = ASTNode::create(NodeType::Document);
  ASTView view(root);

  std::vector<NodeType> visited;
  for (const auto &node : view) {
    visited.push_back(node.type());
  }

  ASSERT_EQ(visited.size(), 1u);
  EXPECT_EQ(visited[0], NodeType::Document);
}

TEST(ASTIterator, FlatChildren) {
  auto root = ASTNode::create(NodeType::Document);
  root->add_child(ASTNode::create(NodeType::Paragraph));
  root->add_child(ASTNode::create(NodeType::Heading));
  root->add_child(ASTNode::create(NodeType::CodeBlock));

  ASTView view(root);

  std::vector<NodeType> visited;
  for (const auto &node : view) {
    visited.push_back(node.type());
  }

  ASSERT_EQ(visited.size(), 4u);
  EXPECT_EQ(visited[0], NodeType::Document);
  EXPECT_EQ(visited[1], NodeType::Paragraph);
  EXPECT_EQ(visited[2], NodeType::Heading);
  EXPECT_EQ(visited[3], NodeType::CodeBlock);
}

TEST(ASTIterator, NestedDFS) {
  // Build:  Doc -> BQ -> Para
  //             -> Heading
  auto doc = ASTNode::create(NodeType::Document);
  auto bq = ASTNode::create(NodeType::BlockQuote);
  auto para = ASTNode::create(NodeType::Paragraph);
  auto heading = ASTNode::create(NodeType::Heading);

  doc->add_child(bq);
  bq->add_child(para);
  doc->add_child(heading);

  ASTView view(doc);

  std::vector<NodeType> visited;
  for (const auto &node : view) {
    visited.push_back(node.type());
  }

  // DFS preorder: Document, BlockQuote, Paragraph, Heading
  ASSERT_EQ(visited.size(), 4u);
  EXPECT_EQ(visited[0], NodeType::Document);
  EXPECT_EQ(visited[1], NodeType::BlockQuote);
  EXPECT_EQ(visited[2], NodeType::Paragraph);
  EXPECT_EQ(visited[3], NodeType::Heading);
}

TEST(ASTIterator, DeepNesting) {
  auto root = ASTNode::create(NodeType::Document);
  auto current = root;

  for (int i = 0; i < 5; i++) {
    auto child = ASTNode::create(NodeType::BlockQuote);
    current->add_child(child);
    current = child;
  }
  // Add a leaf at the bottom
  current->add_child(ASTNode::create(NodeType::Paragraph));

  ASTView view(root);

  int count = 0;
  for ([[maybe_unused]] const auto &node : view) {
    count++;
  }

  // Document + 5 BlockQuotes + 1 Paragraph = 7
  EXPECT_EQ(count, 7);
}

TEST(ASTIterator, ComplexTree) {
  // Build:
  //   Doc
  //   ├─ BQ
  //   │  ├─ Para
  //   │  └─ CodeBlock
  //   └─ List
  //      ├─ Item (with Para child)
  //      └─ Item (with Para child)
  auto doc = ASTNode::create(NodeType::Document);
  auto bq = ASTNode::create(NodeType::BlockQuote);
  auto para1 = ASTNode::create(NodeType::Paragraph);
  auto code = ASTNode::create(NodeType::CodeBlock);
  auto list = ASTNode::create(NodeType::List);
  auto item1 = ASTNode::create(NodeType::Item);
  auto item1_para = ASTNode::create(NodeType::Paragraph);
  auto item2 = ASTNode::create(NodeType::Item);
  auto item2_para = ASTNode::create(NodeType::Paragraph);

  doc->add_child(bq);
  bq->add_child(para1);
  bq->add_child(code);
  doc->add_child(list);
  list->add_child(item1);
  item1->add_child(item1_para);
  list->add_child(item2);
  item2->add_child(item2_para);

  ASTView view(doc);

  std::vector<NodeType> visited;
  for (const auto &node : view) {
    visited.push_back(node.type());
  }

  // DFS preorder:
  // Doc, BQ, Para, CodeBlock, List, Item, Para, Item, Para
  ASSERT_EQ(visited.size(), 9u);
  EXPECT_EQ(visited[0], NodeType::Document);
  EXPECT_EQ(visited[1], NodeType::BlockQuote);
  EXPECT_EQ(visited[2], NodeType::Paragraph);
  EXPECT_EQ(visited[3], NodeType::CodeBlock);
  EXPECT_EQ(visited[4], NodeType::List);
  EXPECT_EQ(visited[5], NodeType::Item);
  EXPECT_EQ(visited[6], NodeType::Paragraph);
  EXPECT_EQ(visited[7], NodeType::Item);
  EXPECT_EQ(visited[8], NodeType::Paragraph);
}
