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
enum NodeFlags : uint16_t {
  NODE_OPEN = 1 << 0,
  NODE_LAST_LINE_BLANK = 1 << 1,
  NODE_CONTENT_UPDATE = 1 << 2,
  NODE_ANNOUNCED = 1 << 3,
  NODE_CLOSE_EMITTED = 1 << 4,
};

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
  bool is_updated() const { return flags_ & NODE_CONTENT_UPDATE; }
  void set_updated(bool v) {
    v ? flags_ |= NODE_CONTENT_UPDATE : flags_ &= ~NODE_CONTENT_UPDATE;
  }
  bool is_announced() const { return flags_ & NODE_ANNOUNCED; }
  void set_announced(bool v) {
    v ? flags_ |= NODE_ANNOUNCED : flags_ &= ~NODE_ANNOUNCED;
  }
  bool is_close_emitted() const { return flags_ & NODE_CLOSE_EMITTED; }
  void set_close_emitted(bool v) {
    v ? flags_ |= NODE_CLOSE_EMITTED : flags_ &= ~NODE_CLOSE_EMITTED;
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

  // Tree edges and relations constructed with children vector
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
  void remove_child(Ptr child) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
      if (*it == child) {
        children_.erase(it);
        return;
      }
    }
  }

  // Metadata access
  template <typename T> T *get_data() { return std::get_if<T>(&data_); }
  template <typename T> const T *get_data() const {
    return std::get_if<T>(&data_);
  }
  template <typename T> void set_data(T &&d) { data_ = std::forward<T>(d); }

  // Text content (accumulated during parsing, read during rendering, update bit
  // is also set here)
  const std::string &content() const { return content_; }
  void append_content(std::string_view text) {
    content_.append(text);
    set_updated(true);
  }
  void set_content(std::string &&text) {
    content_ = std::move(text);
    set_updated(true);
  }
  void clear_content() {
    content_.clear();
    set_updated(true);
  }

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
  using value_type = ASTNode;
  using pointer = ASTNode *;
  using reference = ASTNode &;

  explicit ASTIterator(pointer node) : current_(node) {}

  // struct to refer to nodes as child index of parent's vector
  struct ChildrenOf {
    pointer parent;
    size_t child_idx;
  };

  // DFS traversal
  ASTIterator &operator++();

  // Iterator operators
  reference operator*() const { return *current_; }
  pointer operator->() const { return current_; }

  bool operator==(const ASTIterator &other) const {
    return current_ == other.current_;
  }
  bool operator!=(const ASTIterator &other) const {
    return current_ != other.current_;
  }

private:
  pointer current_;
  std::vector<ChildrenOf> nav_stack_;
};

class ASTView {
private:
  ASTNode::Ptr root_;

public:
  ASTView(ASTNode::Ptr root) : root_(root) {};
  ASTIterator begin() { return ASTIterator(root_.get()); }
  ASTIterator end() { return ASTIterator(nullptr); }
};

#endif // AST_NODE_HPP
