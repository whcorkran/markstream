# AGENTS.md

## What is Markstream?

A C++20 streaming markdown block parser designed for rendering LLM token streams. Parses markdown incrementally line-by-line, producing an AST that can be rendered to HTML as blocks close. The eventual goal is full CommonMark block compliance with a streaming event interface for real-time UI updates.

Targets native applications and WebAssembly. The dual-interface design (callbacks for instant dispatch, polling for batched WASM FFI) supports both use cases.

Inline parsing (bold, italic, links, etc.) is not yet implemented.

## Build

```bash
cmake -S . -B build
cmake --build build
./build/markstream          # reads stdin, parses line-by-line, outputs HTML
./build/markstream_tests    # runs all tests
```

Requires: clang/clang++, C++20, cmake 3.16+. Dependencies fetched automatically (cmark, googletest).

### cmark Dependency

cmark is fetched and linked in CMakeLists.txt but **no source file actually includes cmark headers**. It is not used at runtime. It exists purely as a **reference implementation** — the cmark source (in `build/_deps/cmark-src/src/`) is the consensus fastest C implementation of CommonMark and is valuable for understanding how certain parsing algorithms and data structures should work. When implementing new features or optimizing existing ones, check cmark's source for insight into how the same problem was solved there (e.g., `blocks.c` for the block parser, `inlines.c` for inline parsing, `scanners.re` for scanner patterns). Feel free to study it but do not introduce actual code dependencies on it.

## File Map

```
include/
  ast_node.hpp          ASTNode class, tree structure, metadata types, iterator
  parser.hpp            Parser class (3-phase CommonMark block algorithm)
  scanners.hpp          Block-level scanner functions + character utilities
  html_renderer.hpp     Batch HTML renderer (renders closed AST to HTML)
  events.hpp            BlockEvent struct (Open/Update/Close lifecycle)
  streaming_session.hpp StreamingSession + LineBuffer (mostly unimplemented)

src/
  ast_node.cpp          ASTNode::create, append_child, unlink, iterator++
  parser.cpp            Full 3-phase parsing with try_* block starters
  scanners.cpp          All scanner implementations
  html_renderer.cpp     HtmlRenderer implementation
  streaming_session.cpp LineBuffer implementation + StreamingSession constructor
  main.cpp              Entry point: reads stdin, parses, renders HTML to stdout
  event.cpp             Empty placeholder

tests/
  test_ast_node.cpp     25+ tests: creation, flags, tree ops, metadata, memory
  test_scanners.cpp     ~40 tests: all scanner functions

build/_deps/cmark-src/  Reference cmark source (fetched at build time, not used in code)
```

## Architecture

### Data Flow

```
LLM tokens → LineBuffer (accumulates into complete lines)
                ↓
           Parser::parse_line()
                ↓
     Phase 1: check_open_blocks   (match continuation markers)
     Phase 2: open_new_blocks     (detect new block starts via try_* chain)
     Phase 3: add_text_to_container (add line content to deepest block)
                ↓
           AST (tree of ASTNode, text stored on each node)
                ↓
     HtmlRenderer::render(root)   (traverse closed nodes → HTML)
```

### ASTNode (`include/ast_node.hpp`)

Tree node with smart-pointer ownership:
- `shared_ptr` for first_child, last_child, next (ownership)
- `weak_ptr` for parent, prev (break cycles)
- Factory pattern: `ASTNode::create(NodeType, line, col)` returns `shared_ptr`
- Type-erased metadata via `std::variant<monostate, ListData, CodeData, HeadingData, int>`
- Flags: `NODE_OPEN` (block still accepting content), `NODE_LAST_LINE_BLANK`
- Text content stored directly on node: `content()`, `append_content()`, `set_content()`, `clear_content()`

Nine block types: `Document`, `BlockQuote`, `List`, `Item`, `CodeBlock`, `Heading`, `HtmlBlock`, `Paragraph`, `ThematicBreak`.

### Parser (`src/parser.cpp`)

Implements the CommonMark 3-phase block parsing algorithm. See `build/_deps/cmark-src/src/blocks.c` for cmark's equivalent implementation — the structure is deliberately similar.

**Phase 1 — `check_open_blocks`**: Walk from root down the last-child chain. For each open block, test its continuation condition (e.g., `>` for blockquotes, sufficient indentation for list items). Returns the deepest matched container and whether all blocks matched.

**Phase 2 — `open_new_blocks`**: From the matched container, try each block starter in priority order via small `try_*` functions. Each returns a `BlockStart` enum:
- `None` — didn't match, try next
- `Found` — matched a container block (blockquote, list item), loop continues
- `Leaf` — matched a leaf block (heading, code, html), done

Priority order: blockquote → ATX heading → fenced code → HTML block → setext heading → thematic break → list item → indented code.

All `try_*` functions receive an `OpenBlockCtx` struct bundling the mutable parsing state (container, line, offset, column, etc.).

**Phase 3 — `add_text_to_container`**: Handles lazy continuation (paragraphs extending across unmatched containers). Otherwise finalizes unmatched blocks and adds line content to the deepest container. Creates new paragraphs when no container accepts lines.

**`finalize(node)`**: Closes a block — clears `NODE_OPEN`, trims trailing blank lines from indented code, determines list tight/loose status.

**Text storage**: Each node stores its own text content via `ASTNode::content_`. During parsing, `add_line()` appends text directly to the target node. No external hashmap or shared buffer.

### Scanners (`src/scanners.cpp`)

Pure functions that detect block syntax at a given offset. Return match length (0 = no match). All are in the global namespace except character classifiers which are in `namespace scan`. For reference, cmark's scanner patterns are in `build/_deps/cmark-src/src/scanners.re` (re2c format) — this project implements equivalent logic manually in C++.

Key scanners: `scan_atx_heading_start`, `scan_setext_heading_line`, `scan_open_code_fence`, `scan_close_code_fence`, `scan_thematic_break`, `scan_block_quote_start`, `scan_list_marker`, `scan_html_block_start` (types 1-7), `scan_html_block_end`, `scan_blank_line`, `scan_link_label`.

### HtmlRenderer (`src/html_renderer.cpp`)

Stateless renderer that traverses the AST and outputs HTML. Takes an `ASTNode::Ptr root` directly — no dependency on Parser. Reads text from `node->content()`. Handles tight/loose list rendering, code block language classes, heading levels, HTML escaping.

### LineBuffer (`include/streaming_session.hpp`, `src/streaming_session.cpp`)

Accumulates streaming chunks into complete lines. `feed(string_view)` appends data, `consume_line()` returns the next complete line (up to `\n`). Compacts internal buffer when half consumed.

### StreamingSession (`include/streaming_session.hpp`)

**Mostly unimplemented.** Header declares a dual-interface event system:
- **Callback mode**: `EventCallback` fires on each event
- **Polling mode**: events queue in `std::queue<BlockEvent>`, consumed via `pop_event()`

Only the constructor is implemented. All other methods are declared but undefined.

### BlockEvent (`include/events.hpp`)

Event struct with `Action` (Open/Update/Close), `NodeType`, `depth`, and `html` string. See redesign notes below.

## Known Issues

1. **`event.cpp`** is empty and not compiled
2. **`render_ready_subtree`** was removed (was a dead stub)
3. **StreamingSession** methods beyond the constructor are unimplemented

## Planned Architectural Changes

### 1. Redesign StreamingSession with Lightweight Events

**Problem**: Current `BlockEvent` carries pre-rendered `std::string html`, coupling event dispatch to HTML rendering. The entire StreamingSession is unimplemented anyway.

**Solution**: Events carry structural information only:

```cpp
struct BlockEvent {
    enum Action : uint8_t { Open, Update, Close };
    Action action;
    NodeType type;
    uint8_t depth;
    const ASTNode* node;  // pointer to the relevant node (valid during callback)
};
```

Consumers who want HTML call the renderer separately. This keeps events lightweight (no allocation) and decouples concerns.

StreamingSession implementation:
- `feed(string_view)`: buffer tokens in LineBuffer, extract complete lines, call `parser_.parse_line()`, then `process_tree()` to diff AST state and emit events
- `finish()`: flush remaining buffer, finalize all open blocks, emit Close events
- `process_tree()`: walk AST, emit Open for new nodes, Update for changed content, Close for finalized nodes

### 2. Add Parser Integration Tests

The `md_examples/spec.json` file contains the full CommonMark spec test suite (~652 examples) but no harness runs it yet. A test harness should parse each example's markdown, render to HTML, and compare against the expected output.

### 3. Implement Inline Parsing

Not started. See `build/_deps/cmark-src/src/inlines.c` for reference. The delimiter stack algorithm from the CommonMark spec handles emphasis, strong, code spans, links, images.

## Testing

Run all tests: `./build/markstream_tests`

**Existing coverage**:
- `test_ast_node.cpp`: 25+ tests covering creation, flags, tree ops, metadata, memory safety
- `test_scanners.cpp`: ~40 tests covering all scanner functions

**Not yet tested**: Parser integration, HtmlRenderer output, StreamingSession, LineBuffer.
