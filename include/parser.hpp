#ifndef PARSER_H
#define PARSER_H

#include "node.hpp"  // Includes tree.hpp
#include <cmark.h>
#include <string>

class StreamParser {
    public:
        explicit StreamParser();
        ~StreamParser();
        void parse_line(const std::string& line);

        Node get_root() const {return root;};
        Node get_deepest_open_block() const {return current_block;};
        bool is_complete() const;
        // temporary - needed for cmark_node allocation
        cmark_mem* get_mem() const {return mem;};

    private:
        cmark_mem* mem; // cmark memory allocator

        Node root;
        Node current_block; // deepest open block

        int line_number;
        size_t offset;
        size_t column;
        size_t first_nonspace;
        size_t first_nonspace_column;
        size_t thematic_break_kill_pos;
        int indent;
        bool blank;
        bool partially_consumed_tab;
        size_t last_line_length;

        std::string content;
        std::string curline;

        // line processing
        void find_first_nonspace(const std::string& line);
        void advance_offset(const std::string& line, size_t count, bool columns);
        bool is_line_end_char(char c) const;
        bool is_space_or_tab(char c) const;
        bool is_blank(const std::string& line, size_t offset) const;

        // block continutation checkers
        bool parse_block_quote_prefix(const std::string& line);
        bool parse_node_item_prefix(const std::string& line, Node container);
        bool parse_code_block_prefix(const std::string& line, Node container, bool* should_continue);
        bool parse_html_block_prefix(Node container);

        // block creation
        Node make_block(BlockType tag, int start_column);
        Node add_child(Node parent, BlockType block_type, int start_column);
        Node finalize(Node b);

        // block type checks
        bool can_contain(BlockType parent_type, BlockType child_type) const;
        bool accepts_lines(BlockType block_type) const;
        bool last_child_is_open(Node container) const;

        bool last_line_blank(Node node) const;
        void set_last_line_blank(Node node, bool is_blank);

        // core algorithm
        Node check_open_blocks(const std::string& line, bool* all_matched);
        void open_new_blocks(Node* container, const std::string& line, bool all_matched);
        void add_text_to_container(Node container, Node last_matched_container, const std::string& line);

        // text accumulation
        void add_line(const std::string& line);

        // List parsing
        size_t parse_list_marker(const std::string& input, size_t pos, bool interrupts_paragraph, ListMetadata& data);
        
        // Thematic break
        size_t scan_thematic_break(const std::string& input, size_t offset);
        
        // Utility
        char peek_at(const std::string& input, size_t pos) const;
        void chop_trailing_hashtags(std::string& line);


    };



// Spec:

// We stream in a line (characters followed by /n)

// Depending on the line, the Document is modified like so:

    // One or more open blocks may be closed.  

    // One or more new blocks may be created as children of the last open block.
    // Text may be added to the last (deepest) open block remaining on the tree.


// For each line, we follow this procedure:

//     First we iterate through the open blocks, starting with the root document, and descending through last children down to the last open block. 
//     Each block imposes a condition that the line must satisfy if the block is to remain open. 
//     For example, a block quote requires a > character. 
//     A paragraph requires a non-blank line. 
//     In this phase we may match all or just some of the open blocks. But we cannot close unmatched blocks yet, because we may have a lazy continuation line.


//     Next, after consuming the continuation markers for existing blocks, we look for new block starts (e.g. > for a block quote. 
//     If we encounter a new block start, we close any blocks unmatched in step 1 before creating the new block as a child of the last matched block.


//     Finally, we look at the remainder of the line (after block markers like >, list markers, and indentation have been consumed). 
//     This is text that can be incorporated into the last open block (a paragraph, code block, heading, or raw HTML).

// Reference link definitions are detected when a paragraph is closed; 
// the accumulated text lines are parsed to see if they begin with one or more reference link definitions. Any remainder becomes a normal paragraph.
#endif // PARSER_H