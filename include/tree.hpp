#ifndef TREE_H
#define TREE_H

#include <string>
#include <node.h>
#include <chunk.h>


// ============================================================================
// C++ replacements for cmark data structures
// These will replace cmark types when we remove the dependency
// ============================================================================

// List type enum - C++ replacement for cmark_list_type
// Note: Values match cmark constants (CMARK_BULLET_LIST=1, CMARK_ORDERED_LIST=2)
enum class ListType : unsigned char {
    Bullet = 1,   // CMARK_BULLET_LIST
    Ordered = 2   // CMARK_ORDERED_LIST
};

// Delimiter type enum - C++ replacement for cmark_list_delim
// Note: Values match cmark constants (CMARK_NO_DELIM=0, CMARK_PERIOD_DELIM=1, CMARK_PAREN_DELIM=2)
enum class DelimType : unsigned char {
    None = 0,     // CMARK_NO_DELIM
    Period = 1,   // CMARK_PERIOD_DELIM
    Paren = 2     // CMARK_PAREN_DELIM
};

// Convert ListType to cmark_list_type
inline unsigned char to_cmark_list_type(ListType type) {
    return static_cast<unsigned char>(type);
}

// Convert cmark_list_type to ListType
inline ListType from_cmark_list_type(unsigned char type) {
    // CMARK_NO_LIST = 0, CMARK_BULLET_LIST = 1, CMARK_ORDERED_LIST = 2
    if (type == 1) return ListType::Bullet;
    if (type == 2) return ListType::Ordered;
    return ListType::Bullet; // fallback
}

// Convert DelimType to cmark_list_delim
inline unsigned char to_cmark_delim_type(DelimType type) {
    return static_cast<unsigned char>(type);
}

// Convert cmark_list_delim to DelimType
inline DelimType from_cmark_delim_type(unsigned char delim) {
    // CMARK_NO_DELIM = 0, CMARK_PERIOD_DELIM = 1, CMARK_PAREN_DELIM = 2
    if (delim == 0) return DelimType::None;
    if (delim == 1) return DelimType::Period;
    if (delim == 2) return DelimType::Paren;
    return DelimType::None; // fallback
}

// List metadata - C++ replacement for cmark_list
struct ListMetadata {
    int marker_offset = 0;
    int padding = 0;
    int start = 0;
    ListType list_type = ListType::Bullet;
    DelimType delimiter = DelimType::None;
    unsigned char bullet_char = 0;
    bool tight = false;
    
    // Convert to cmark_list for assignment to cmark_node
    // Temporary until we remove cmark dependency
    cmark_list to_cmark_list() const {
        cmark_list result;
        result.marker_offset = marker_offset;
        result.padding = padding;
        result.start = start;
        result.list_type = to_cmark_list_type(list_type);
        result.delimiter = to_cmark_delim_type(delimiter);
        result.bullet_char = bullet_char;
        result.tight = tight;
        return result;
    }
    
    // Create from cmark_list (for reading from cmark_node)
    // Temporary until we remove cmark dependency
    static ListMetadata from_cmark_list(const cmark_list& list) {
        ListMetadata result;
        result.marker_offset = list.marker_offset;
        result.padding = list.padding;
        result.start = list.start;
        result.list_type = from_cmark_list_type(list.list_type);
        result.delimiter = from_cmark_delim_type(list.delimiter);
        result.bullet_char = list.bullet_char;
        result.tight = list.tight;
        return result;
    }
    
    // Check if two list metadata match (for list continuation)
    bool matches(const ListMetadata& other) const {
        return list_type == other.list_type &&
               delimiter == other.delimiter &&
               bullet_char == other.bullet_char;
    }
};

// Helper to convert std::string to cmark_chunk for scanner function calls
// Temporary until we remove cmark dependency
inline cmark_chunk to_chunk(const std::string& s) {
    cmark_chunk ch;
    ch.data = reinterpret_cast<const unsigned char*>(s.data());
    ch.len = static_cast<bufsize_t>(s.size());
    return ch;
}

// Block type enum
enum class BlockType {
    Document,
    BlockQuote,
    List,
    Item,
    CodeBlock,
    Heading,
    HtmlBlock,
    Paragraph,
    ThematicBreak
};

// Convert BlockType to cmark_node_type
inline cmark_node_type to_cmark_node_type(BlockType type) {
    switch (type) {
        case BlockType::Document:        return CMARK_NODE_DOCUMENT;
        case BlockType::BlockQuote:      return CMARK_NODE_BLOCK_QUOTE;
        case BlockType::List:            return CMARK_NODE_LIST;
        case BlockType::Item:            return CMARK_NODE_ITEM;
        case BlockType::CodeBlock:       return CMARK_NODE_CODE_BLOCK;
        case BlockType::Heading:         return CMARK_NODE_HEADING;
        case BlockType::HtmlBlock:       return CMARK_NODE_HTML_BLOCK;
        case BlockType::Paragraph:       return CMARK_NODE_PARAGRAPH;
        case BlockType::ThematicBreak:   return CMARK_NODE_THEMATIC_BREAK;
        default:                         return CMARK_NODE_DOCUMENT;
    }

}

// Convert cmark_node_type to BlockType (for reading from nodes)
inline BlockType from_cmark_node_type(cmark_node_type type) {
    switch (type) {
        case CMARK_NODE_DOCUMENT:        return BlockType::Document;
        case CMARK_NODE_BLOCK_QUOTE:     return BlockType::BlockQuote;
        case CMARK_NODE_LIST:            return BlockType::List;
        case CMARK_NODE_ITEM:            return BlockType::Item;
        case CMARK_NODE_CODE_BLOCK:      return BlockType::CodeBlock;
        case CMARK_NODE_HEADING:         return BlockType::Heading;
        case CMARK_NODE_HTML_BLOCK:      return BlockType::HtmlBlock;
        case CMARK_NODE_PARAGRAPH:       return BlockType::Paragraph;
        case CMARK_NODE_THEMATIC_BREAK:  return BlockType::ThematicBreak;
        default:                         return BlockType::Document; // fallback
    }
}



#endif // TREE_H
