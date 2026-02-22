#ifndef AST_NODE_HPP
#define AST_NODE_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class NodeType : uint8_t {
  Document = 0,
  BlockQuote,
  List,
  Item,
  CodeBlock,
  Heading,
  HtmlBlock,
  Paragraph,
  ThematicBreak
};

// Block-specific metadata (only what varies per block type)
struct ListData {
  int start;         // Starting number (ordered lists)
  int marker_offset; // Indentation before marker
  int padding;       // Spaces after marker (content indent)
  char marker_char;  // '-', '*', '+' for bullet; '.' or ')' for ordered
  bool is_ordered;   // true for ordered lists
  bool is_tight;     // Tight vs loose list

  // Check if two lists match (for continuation)
  bool matches(const ListData &other) const {
    return is_ordered == other.is_ordered && marker_char == other.marker_char;
  }
};

struct CodeData {
  std::string info;     // Language/info string
  uint8_t fence_length; // 0 = indented, 3+ = fenced
  uint8_t fence_offset; // Indentation of opening fence
  char fence_char;      // '`' or '~'

  bool is_fenced() const { return fence_length > 0; }
};

struct HeadingData {
  uint8_t level; // 1-6
  bool setext;   // true = setext, false = ATX
};

// Node flags
enum NodeFlags : uint16_t { NODE_OPEN = 1 << 0, NODE_LAST_LINE_BLANK = 1 << 1 };

class ASTNode : public std::enable_shared_from_this<ASTNode> {
public:
  // Only access the key from this class, effectively makes constructor private
  struct ConstructorKey {
    explicit ConstructorKey() = default;
  };

  ASTNode(ConstructorKey, NodeType type, int line, int col)
      : type_(type), flags_(NODE_OPEN), start_line_(line), start_col_(col),
        end_line_(0), end_col_(0) {}

  using Ptr = std::shared_ptr<ASTNode>;
  using WeakPtr = std::weak_ptr<ASTNode>;
  using Metadata =
      std::variant<std::monostate, ListData, CodeData, HeadingData, int>;

  // Factory (nodes always created as shared_ptr)
  static Ptr create(NodeType type, int line = 0, int col = 0);

  // Type
  NodeType type() const { return type_; }

  // Flags
  bool is_open() const { return flags_ & NODE_OPEN; }
  void set_open(bool v) { v ? flags_ |= NODE_OPEN : flags_ &= ~NODE_OPEN; }
  bool last_line_blank() const { return flags_ & NODE_LAST_LINE_BLANK; }
  void set_last_line_blank(bool v) {
    v ? flags_ |= NODE_LAST_LINE_BLANK : flags_ &= ~NODE_LAST_LINE_BLANK;
  }

  // Position
  int start_line() const { return start_line_; }
  int start_col() const { return start_col_; }
  int end_line() const { return end_line_; }
  int end_col() const { return end_col_; }
  void set_start(int line, int col) {
    start_line_ = line;
    start_col_ = col;
  }
  void set_end(int line, int col) {
    end_line_ = line;
    end_col_ = col;
  }

  // Tree navigation
  const std::vector<Ptr> &children() { return children_; }
  const std::vector<Ptr> &children() const;
  Ptr first_child() const {
    return children_.empty() ? nullptr : children_.front();
  }
  Ptr last_child() const {
    return children_.empty() ? nullptr : children_.back();
  }

  void add_child(Ptr child) { children_.push_back(child); }
  void replace_last_child(Ptr child) {
    children_.pop_back();
    children_.push_back(child);
  }

  // Metadata access
  template <typename T> T *get_data() { return std::get_if<T>(&data_); }
  template <typename T> const T *get_data() const {
    return std::get_if<T>(&data_);
  }
  template <typename T> void set_data(T &&d) { data_ = std::forward<T>(d); }

  // Text content (accumulated during parsing, read during rendering)
  const std::string &content() const { return content_; }
  void append_content(std::string_view text) { content_.append(text); }
  void set_content(std::string &&text) { content_ = std::move(text); }
  void clear_content() { content_.clear(); }

private:
  NodeType type_;
  uint16_t flags_;
  int start_line_, start_col_, end_line_, end_col_;

  std::vector<Ptr> children_;
  Metadata data_;
  std::string content_;
};

// Iterator over tree
class ASTIterator {
public:
  // STL tags
  using iterator_category = std::forward_iterator_tag;
  using value_type = const ASTNode;
  using pointer = const ASTNode *;
  using reference = const ASTNode &;

  explicit ASTIterator(pointer node) : current_(node) {
    nav_stack_.push_back({current_, 0});
  }

  // struct to refer to nodes as child index of parent's vector
  struct ChildOf {
    pointer parent;
    size_t child_idx;
  };
  // DFS traversal
  ASTIterator &operator++();

  // Iterator operators
  reference operator*() const { return *current_; }
  pointer operator->() const { return current_; }

  bool operator==(ASTIterator &other) const {
    return current_ == other.current_;
  }
  bool operator!=(ASTIterator &other) const {
    return current_ != other.current_;
  }

private:
  pointer current_;
  std::vector<ChildOf> nav_stack_;
};

class ASTView {
private:
  ASTNode::Ptr root_;

public:
  ASTView(ASTNode::Ptr root) : root_(root) {};
  ASTIterator begin() const { return ASTIterator(root_.get()); }
  ASTIterator end() const { return ASTIterator(nullptr); }
};

#endif // AST_NODE_HPP
