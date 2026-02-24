# AGENTS.md

## What is Markstream?

A C++20 streaming markdown parser optimized for rendering LLM token streams in real time. Parses markdown incrementally -- token by token, line by line -- producing a forward-only AST where blocks are immutable once closed. Designed for native applications and WebAssembly.

Data flows in one direction: tokens stream in, the parser updates the AST, events are emitted, and closed blocks are never revisited. This forward-only invariant enables aggressive performance optimizations -- no random access, no back-patching, no retroactive edits to finalized output.

Inline parsing is now implemented for a core CommonMark subset and integrated into HTML rendering. Current support includes emphasis/strong, code spans, escapes, entities, inline links/images, autolinks, inline HTML passthrough, and soft/hard line breaks. Reference-style links and smart punctuation are not implemented yet.

## Design Principles

1. **CommonMark compliance.** The block parsing algorithm follows the CommonMark spec. Full spec compliance is the target (currently block-level only).

2. **Forward-only data flow.** Tokens arrive, the AST is updated, events fire. Closed blocks are immutable. The program never looks backward to edit finalized state. Data structures and algorithms exploit this constraint for performance.

3. **First-class streaming.** Built for small, rapidly arriving fragments (LLM output tokens). Not a batch parser retrofitted for streaming -- streaming is the primary use case.

4. **Dual dispatch interface.** Callback mode for instant event dispatch in native applications. Polling mode for batched consumption across the WASM/JS boundary with minimal FFI overhead.

5. **Event-driven architecture.** All state changes emit lightweight structural events (Open/Update/Close). Consumers decide how to react -- render HTML, update a UI, build their own data structures.

6. **WASM as a core target.** Not an afterthought. The polling interface, batched event consumption, and minimal-allocation event design exist specifically for efficient WASM FFI.

## Build

```bash
cmake -S . -B build
cmake --build build
./build/markstream          # reads stdin, parses line-by-line, outputs HTML
./build/markstream_tests    # runs all tests
```

Requires: clang/clang++, C++20, cmake 3.16+. Dependencies fetched automatically (googletest). cmark is fetched as a reference implementation only (see below).

### cmark Reference

cmark is fetched via CMake's FetchContent and currently linked by CMake, but **no Markstream source file includes or calls into cmark**. It exists as a **reference implementation** -- the cmark source (in `build/_deps/cmark-src/src/`) is the canonical fast C implementation of CommonMark and is invaluable for understanding parsing algorithms. Key reference files:

- `blocks.c` -- block parsing algorithm (the 3-phase structure our Parser mirrors)
- `inlines.c` -- inline parsing reference (Markstream now has a C++ inline parser in `src/inline.cpp`)
- `scanners.re` -- scanner patterns in re2c format (we implement equivalent logic manually)

Study it freely but do not introduce runtime dependencies on it. The cmark link in CMakeLists.txt should eventually be removed.

## File Map

```
include/
  ast_node.hpp          ASTNode class, vector-based children, metadata, DFS iterator
  parser.hpp            Parser class (3-phase CommonMark block algorithm)
  scanners.hpp          Block-level scanner functions + character utilities
  inline.hpp            Inline parser entry point (`render_inlines_html`)
  html_renderer.hpp     HTML renderer (renders AST subtrees to HTML)
  events.hpp            BlockEvent struct (Open/Update/Close lifecycle)
  streaming_session.hpp StreamingSession (entry point) + LineBuffer

src/
  ast_node.cpp          ASTNode::create, ASTIterator::operator++
  parser.cpp            Full 3-phase parsing with try_* block starters
  scanners.cpp          All scanner implementations
  inline.cpp            Core inline parser + inline HTML renderer
  html_renderer.cpp     HtmlRenderer implementation
  streaming_session.cpp LineBuffer + StreamingSession implementation
  main.cpp              CLI entry point: stdin -> parse -> HTML to stdout
  event.cpp             Empty placeholder (not compiled)

tests/
  test_ast_node.cpp     31 ASTNode + 5 ASTIterator tests (vector API)
  test_scanners.cpp     ~40 tests: all scanner functions (working)
  test_inline.cpp       Inline parsing/rendering tests
  test_streaming_session.cpp 8 StreamingSession lifecycle/event tests

build/_deps/cmark-src/  Reference cmark source (fetched at build time, not used in code)
```

## Architecture

### Data Flow

```
LLM tokens
    |
    v
StreamingSession::parse(token)
    |
    v
LineBuffer  (accumulates tokens into complete lines)
    |
    v
Parser::parse_line(line)
    |
    +---> Phase 1: check_open_blocks   (match continuation markers top-down)
    +---> Phase 2: open_new_blocks     (detect new block starts via try_* chain)
    +---> Phase 3: add_text_to_container (add line content to deepest block)
    |
    v
AST updated (vector-based tree of ASTNode, text on each node)
    |
    v
StreamingSession::process_tree()  (diff AST, detect new/changed/closed nodes)
    |
    v
BlockEvent emitted  (Open / Update / Close)
    |
    +---> Callback mode: instant dispatch via EventCallback
    +---> Polling mode:  queued in std::queue, consumed via pop_event()/pop_events()
    |
    v
Consumer renders / updates UI / etc.
    (HtmlRenderer available for HTML output, or consumers handle events directly)
```

### Core Classes

#### StreamingSession (`include/streaming_session.hpp`)

The public entry point. Owns a LineBuffer, a Parser, and the event dispatch machinery.

**Input interface:**
- `parse(string_view token)` -- feed a token/chunk, triggers line extraction, parsing, and event emission
- `finish()` -- signal end of input, flush buffer, close all open blocks, emit final Close events
- `reset()` -- clear all state for reuse with a new document

**Output interface (dual dispatch):**
- **Callback mode:** provide an `EventCallback` at construction or via `set_callback()`. Events fire immediately during `parse()`/`finish()`.
- **Polling mode:** omit the callback. Events queue internally. Consume via `has_events()`, `pop_event()`, `pop_events(max_count)`. The `pop_events()` batch method is specifically designed for efficient WASM FFI -- one call across the boundary returns multiple events.

**Configuration:**
- `set_emit_updates(bool)` -- toggle Update events for content changes (default: on)

**Internal flow per `parse()` call:**
1. Feed token to LineBuffer
2. Extract all complete lines (loop, not just one)
3. For each line, call `parser_.parse_line()`
4. After parsing, call `process_tree()` to diff AST state and emit events

**`process_tree()` design:** Walk the AST, compare against `announced_` and `closed_`, emit Open for newly seen nodes, emit Update for open text blocks with `NODE_CONTENT_UPDATE` set (when `emit_updates_` is enabled), clear the update bit, and emit Close once when a node becomes closed. This is a lightweight incremental diff -- not a full tree diff. Because data flows forward only, the diff is simple: new nodes appear, existing nodes gain content, and closed nodes are finalized forever.

**Lifecycle behavior:**
- `parse()` throws `std::logic_error` if called after `finish()`.
- `parse()` throws `std::length_error` if `LineBuffer` exceeds max size.
- `finish()` is idempotent and flushes any trailing partial line.
- `reset()` clears parser/buffer/event state and allows reuse.

#### Parser (`src/parser.cpp`)

Implements the CommonMark 3-phase block parsing algorithm. Structure mirrors cmark's `blocks.c` deliberately.

**Phase 1 -- `check_open_blocks`:** Walk from root down the last-child chain. For each open block, test its continuation condition (e.g., `>` for blockquotes, indentation for list items). Returns the deepest matched container and whether all blocks matched.

**Phase 2 -- `open_new_blocks`:** From the matched container, try each block starter in priority order via `try_*` functions. Each returns a `BlockStart` enum:
- `None` -- didn't match, try next
- `Found` -- matched a container block (blockquote, list item), loop continues
- `Leaf` -- matched a leaf block (heading, code fence, HTML block), done

Priority order: blockquote > ATX heading > fenced code > HTML block > setext heading > thematic break > list item > indented code.

All `try_*` functions receive an `OpenBlockCtx` struct bundling the mutable parsing state.

**Phase 3 -- `add_text_to_container`:** Handles lazy continuation (paragraphs extending across unmatched containers). Finalizes unmatched blocks, adds line content to the deepest container. Creates new paragraphs when no container accepts lines.

**`finalize(node)`:** Closes a block -- clears `NODE_OPEN`, trims trailing blank lines from indented code, determines list tight/loose status.

**End-of-stream hooks:** `finish_document()` finalizes all open blocks and the document root at EOF; `reset()` rebuilds parser state for a new document.

**`open_blocks_` stack:** The parser maintains an explicit `std::vector<ASTNode::Ptr> open_blocks_` stack where `open_blocks_[0]` is the root Document and `open_blocks_.back()` is the deepest open block. This replaces the old parent-pointer walking. Phase 1 iterates the stack to check continuation; `add_child()` and `finalize()` manipulate the stack directly. The `pre_phase2_depth_` member tracks the stack size before phase 2 runs, so phase 3 can distinguish pre-existing unmatched blocks from newly created ones when finalizing.

#### ASTNode (`include/ast_node.hpp`)

Tree node with `shared_ptr` ownership. Children stored in `std::vector<Ptr>` for cache-friendly traversal and fast incremental updates.

**Factory pattern:** `ASTNode::create(NodeType, line, col)` returns `shared_ptr`. Constructor is private (enforced via `ConstructorKey` pattern).

**Children API:**
- `children()` -- returns `const vector<Ptr>&`
- `first_child()`, `last_child()` -- convenience accessors
- `add_child(Ptr)` -- appends to children vector
- `replace_last_child(Ptr)` -- used for setext heading transformation (replaces Paragraph with Heading)

No parent, next, or prev pointers. Navigation up the tree is handled by the Parser's open block stack, not by the nodes themselves. This keeps nodes lightweight and forward-only.

**Metadata:** Type-erased via `std::variant<monostate, ListData, CodeData, HeadingData, int>`. Accessed via `get_data<T>()` / `set_data<T>()`.

**Flags:** `NODE_OPEN` (block still accepting content), `NODE_LAST_LINE_BLANK` (for tight/loose list detection), `NODE_CONTENT_UPDATE` (content changed since last StreamingSession diff pass).

**Content:** Text stored directly on each node (`std::string content_`). During parsing, `add_line()` appends text. Once the block closes, content is immutable.

**Nine block types:** `Document`, `BlockQuote`, `List`, `Item`, `CodeBlock`, `Heading`, `HtmlBlock`, `Paragraph`, `ThematicBreak`.

**ASTIterator:** STL-compatible forward iterator for DFS preorder traversal. Uses an internal `vector<ChildrenOf>` stack to track position within each node's children vector (no parent pointers needed). The current iterator yields mutable nodes, which StreamingSession uses to clear update flags during tree diffing.

#### HtmlRenderer (`src/html_renderer.cpp`)

Stateless renderer that traverses an AST subtree and outputs HTML. Takes an `ASTNode::Ptr` -- no dependency on Parser or StreamingSession. Reads text from `node->content()`.

Handles: tight/loose list rendering, ordered list start attributes, code block language classes (`class="language-X"`), heading levels, HTML block raw passthrough, thematic breaks, and inline rendering via `render_inlines_html()` for paragraph/heading/tight-list paragraph text.

#### Inline Parser (`include/inline.hpp`, `src/inline.cpp`)

Standalone inline parser/renderer module inspired by cmark's delimiter-stack algorithm and implemented with cache-friendly C++ vectors.

**Current support:**
- Emphasis and strong emphasis (`*`, `_`) with flanking and modulo-3 rules
- Code spans with CommonMark normalization behavior
- Backslash escapes
- Entity decoding (named + numeric)
- Inline links/images (`[text](url "title")`, `![alt](src)`)
- Autolinks (`<https://...>`, `<user@example.com>`)
- Inline HTML passthrough
- Soft/hard line breaks

**Not yet implemented:**
- Reference-style links (`[text][id]`, `[id]: ...`)
- Smart punctuation transforms

Consumers are free to use HtmlRenderer to convert closed blocks to HTML, or to implement their own rendering from the event stream and AST node pointers.

#### BlockEvent (`include/events.hpp`)

Lightweight event struct -- no allocations, no pre-rendered strings:

```cpp
struct BlockEvent {
    enum Action : uint8_t { Open, Update, Close };
    Action action;
    NodeType type;
    const ASTNode* node;
};
```

Events carry structural information only. Consumers who want HTML call HtmlRenderer separately. This decouples event dispatch from rendering and keeps events cheap to create and dispatch.

#### LineBuffer (`include/streaming_session.hpp`, `src/streaming_session.cpp`)

Accumulates streaming token chunks into complete lines. `feed(string_view)` appends data, `consume_line()` returns the next complete line (up to `\n`). Compacts internal buffer when the consumed portion exceeds half the buffer. Has a configurable max buffer size (default 1MB) to prevent unbounded growth.

#### Scanners (`src/scanners.cpp`)

Pure functions that detect block syntax at a given offset in a line. Return match length (0 = no match). Character classifiers live in `namespace scan`; block-level scanners are in the global namespace. cmark's `scanners.re` is the reference; this project implements equivalent logic in manual C++.

Key scanners: `scan_atx_heading_start`, `scan_setext_heading_line`, `scan_open_code_fence`, `scan_close_code_fence`, `scan_thematic_break`, `scan_block_quote_start`, `scan_list_marker`, `scan_html_block_start` (types 1-7), `scan_html_block_end`, `scan_blank_line`, `scan_link_label`.

## Current State and Known Issues

The vector-based AST refactor (Phase 1), StreamingSession implementation (Phase 2), and core inline parser integration are complete. The codebase compiles cleanly and all 112 tests pass. The parser, renderer, and tests all use the vector children API with no remaining references to the old linked-list API (`parent()`, `next()`, `prev()`, `unlink()`, `append_child()`).

### Remaining Issues

1. **`event.cpp` is empty** and not in the CMakeLists.txt compile list.

2. **`scan_thematic_break` accepts backslash.** Line 273 of `scanners.cpp` allows `\\` between thematic break markers, which is not valid per CommonMark.

3. **Block renderer escaping helper still incomplete.** `HtmlRenderer::escape_html()` does not escape `'` (single quote). Note: inline rendering paths do escape `'` for attribute contexts.

4. **cmark linked unnecessarily.** CMakeLists.txt links cmark via `target_link_libraries` despite no source file including cmark headers. This adds unnecessary build coupling.

## Implementation Roadmap

### Phase 1: Complete the AST Refactor (DONE)

The vector-based tree transition is complete:
- [x] Added `open_blocks_` stack to Parser (replaces parent-pointer walking)
- [x] Rewrote parser.cpp to use the stack for all navigation and finalization
- [x] Updated html_renderer.cpp to iterate via `children()` vector
- [x] Rewrote test_ast_node.cpp for the vector API (31 ASTNode + 5 ASTIterator tests)
- [x] Fixed the const `children()` definition in ast_node.cpp
- [x] Rewrote ASTIterator `operator++` with correct DFS traversal logic
- [x] Fixed ASTIterator comparison operators to accept `const` refs

### Phase 2: Implement StreamingSession (DONE)

Completed streaming pipeline work:
- [x] `parse()` loops over all available lines
- [x] `process_tree()` diffs AST against `announced_` and `closed_`
- [x] `emit()` handles both callback and queue dispatch (null-safe)
- [x] Implemented `finish()`, `reset()`, `pop_event()`, `pop_events()`
- [x] Added parser end-of-stream hooks: `finish_document()` and `reset()`
- [x] Added dedicated StreamingSession tests

### Phase 3: Parser Integration Tests

Build a test harness that runs the CommonMark spec test suite (`md_examples/spec.json`, ~652 examples). Parse each example, render to HTML, compare against expected output. This is the ground truth for correctness.

### Phase 4: Inline Parsing (IN PROGRESS)

Core inline parsing is implemented in `src/inline.cpp` and integrated into `HtmlRenderer`. Current implementation covers emphasis/strong, code spans, escapes, entities, inline links/images, autolinks, inline HTML, and line breaks.

Remaining inline work:
- Reference-style links and reference map integration
- Smart punctuation parity
- Full CommonMark conformance validation via spec harness

## Testing

```bash
./build/markstream_tests    # runs all tests
```

**Working tests:**
- `test_scanners.cpp`: ~40 tests covering all scanner functions. No dependency on ASTNode -- these work.
- `test_ast_node.cpp`: 31 ASTNode tests (creation, flags, position, vector children, content, metadata, memory management, edge cases) + 5 ASTIterator tests (single node, flat children, nested DFS, deep nesting, complex tree).
- `test_streaming_session.cpp`: 8 tests covering callback/polling dispatch, Update toggle, idempotent finish, close deduplication, reset/reuse, and error behavior.
- `test_inline.cpp`: core inline parsing/rendering tests (emphasis, code spans, entities/escapes, links/images, autolinks, inline HTML, line breaks).

**Not yet tested:**
- Parser integration (no spec harness yet)
- Full CommonMark inline conformance (spec harness not yet wired)
- LineBuffer stress/edge cases
