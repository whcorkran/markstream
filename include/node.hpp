#ifndef NODE_H
#define NODE_H

#include <cmark.h>
#include <node.h>
#include "tree.hpp"
#include <cstdint>
#include <cstddef>

// Thin wrapper around cmark_node for easier migration to custom tree later
class Node {
public:
    // Construct from existing cmark_node (non-owning)
    explicit Node(cmark_node* node) : node_(node) {}
    
    // Create new node (owning - will be freed when cmark_node is freed)
    Node(cmark_mem* mem, BlockType type, int start_line, int start_column);
    
    // Default constructor - null node
    Node() : node_(nullptr) {}
    
    // Access underlying cmark_node (for compatibility with cmark functions)
    cmark_node* get_cmark_node() const { return node_; }
    cmark_node* operator->() const { return node_; }
    operator cmark_node*() const { return node_; }
    
    // Check if node is valid
    bool is_null() const { return node_ == nullptr; }
    explicit operator bool() const { return node_ != nullptr; }
    
    // Node type
    BlockType get_type() const;
    void set_type(BlockType type);
    
    // Node flags
    bool is_open() const;
    void set_open(bool open);
    bool last_line_blank() const;
    void set_last_line_blank(bool blank);
    
    // Tree navigation
    Node get_parent() const;
    Node get_first_child() const;
    Node get_last_child() const;
    Node get_next() const;
    Node get_prev() const;
    
    // Tree structure
    void set_parent(Node parent);
    void set_first_child(Node child);
    void set_last_child(Node child);
    void set_next(Node next);
    void set_prev(Node prev);
    
    // Position information
    int get_start_line() const { return node_ ? node_->start_line : 0; }
    int get_start_column() const { return node_ ? node_->start_column : 0; }
    int get_end_line() const { return node_ ? node_->end_line : 0; }
    int get_end_column() const { return node_ ? node_->end_column : 0; }
    void set_start_line(int line) { if (node_) node_->start_line = line; }
    void set_start_column(int col) { if (node_) node_->start_column = col; }
    void set_end_line(int line) { if (node_) node_->end_line = line; }
    void set_end_column(int col) { if (node_) node_->end_column = col; }
    
    // List metadata (for List and Item nodes)
    ListMetadata get_list_metadata() const;
    void set_list_metadata(const ListMetadata& metadata);
    
    // Code block metadata
    struct CodeMetadata {
        unsigned char* info = nullptr;
        uint8_t fence_length = 0;
        uint8_t fence_offset = 0;
        unsigned char fence_char = 0;
        int8_t fenced = 0;
    };
    CodeMetadata get_code_metadata() const;
    void set_code_metadata(const CodeMetadata& metadata);
    
    // Heading metadata
    struct HeadingMetadata {
        int internal_offset = 0;
        int8_t level = 1;
        bool setext = false;
    };
    HeadingMetadata get_heading_metadata() const;
    void set_heading_metadata(const HeadingMetadata& metadata);
    
    // HTML block type
    int get_html_block_type() const;
    void set_html_block_type(int type);
    
    // Content data
    const char* get_data() const;
    size_t get_data_len() const;
    void set_data(const char* data, size_t len);
    
    // Comparison
    bool operator==(const Node& other) const { return node_ == other.node_; }
    bool operator!=(const Node& other) const { return node_ != other.node_; }
    
private:
    cmark_node* node_;
};

// Helper to create a new node
inline Node make_node(cmark_mem* mem, BlockType type, int start_line, int start_column) {
    return Node(mem, type, start_line, start_column);
}

#endif // NODE_H

