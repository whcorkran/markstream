# Markstream

A C++20 streaming markdown parser built for rendering LLM output tokens in real time.

Markstream parses markdown incrementally -- token by token, line by line -- producing a forward-only AST where blocks are immutable once closed. It emits lightweight structural events (Open, Update, Close) as the AST evolves, enabling consumers to render or react to changes the instant they happen.

Designed for native applications and WebAssembly.

## Why Markstream?

Most markdown parsers are batch processors: they take a complete document, parse it, and return an AST or HTML string. That model breaks down when you need to render output from a large language model as tokens arrive -- often one word or partial word at a time, at high speed.

Markstream is built streaming-first:

- **Tokens in, events out.** Feed fragments of any size. Complete lines are extracted, parsed, and diffed automatically. Events fire as blocks open, receive content, and close.
- **Forward-only invariant.** Once a block closes, it is never revisited or modified. Data structures and algorithms exploit this constraint for performance -- no random access, no back-patching.
- **Dual dispatch.** Callback mode for instant event-driven rendering in native applications. Polling mode with batched consumption for efficient WASM FFI.
- **CommonMark compliant.** The block parsing algorithm follows the [CommonMark specification](https://spec.commonmark.org/). Full block-level compliance is the immediate target. Inline parsing (bold, italic, links, etc.) will be layered on afterward.

## Quick Start

### Build

```bash
cmake -S . -B build
cmake --build build
```

### Run

```bash
# Parse stdin, output HTML
echo "# Hello World" | ./build/markstream

# Run tests
./build/markstream_tests
```

### Requirements

- clang/clang++ with C++20 support
- CMake 3.16+
- Dependencies are fetched automatically (googletest)

## Usage

### Callback Mode (Native Applications)

Events fire immediately as parsing progresses -- ideal for real-time UI updates.

```cpp
#include "streaming_session.hpp"
#include "html_renderer.hpp"

StreamingSession session([](const BlockEvent& ev) {
    switch (ev.action) {
        case BlockEvent::Open:
            // A new block has started -- allocate a UI element
            break;
        case BlockEvent::Update:
            // Block content changed -- re-render this block
            // ev.node->content() has the latest text
            break;
        case BlockEvent::Close:
            // Block is finalized and immutable
            // Render final HTML if needed:
            HtmlRenderer renderer;
            std::string html = renderer.render(ev.node->shared_from_this());
            break;
    }
});

// Feed tokens as they arrive from an LLM
for (const auto& token : llm_stream) {
    session.parse(token);
}
session.finish();
```

### Polling Mode (WASM / Batched Consumption)

Events accumulate in an internal queue. Consume them in batches to minimize FFI overhead.

```cpp
StreamingSession session;  // no callback = polling mode

// Feed tokens
session.parse(token);

// Consume events in batches (efficient across WASM boundary)
while (session.has_events()) {
    auto events = session.pop_events(64);  // up to 64 events per call
    for (const auto& ev : events) {
        process(ev);
    }
}

session.finish();
```

### Direct Parser + Renderer (Batch Processing)

For simple use cases that don't need streaming events.

```cpp
#include "parser.hpp"
#include "html_renderer.hpp"

Parser parser;
parser.parse_line("# Hello\n");
parser.parse_line("Some paragraph text.\n");

HtmlRenderer renderer;
std::string html = renderer.render(parser.get_root());
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
LineBuffer          accumulates tokens into complete lines
    |
    v
Parser::parse_line(line)
    |
    +--- Phase 1: check_open_blocks     match continuation markers top-down
    +--- Phase 2: open_new_blocks       detect new block starts
    +--- Phase 3: add_text_to_container  add line content to deepest block
    |
    v
AST updated         vector-based tree of ASTNode
    |
    v
process_tree()      diff AST against previous state
    |
    v
BlockEvent          Open / Update / Close
    |
    +--- Callback:  instant dispatch via EventCallback
    +--- Polling:   queued, consumed via pop_event() / pop_events()
    |
    v
Consumer            render HTML, update UI, build data structures, etc.
```

### Core Components

| Component | Role |
|-----------|------|
| **StreamingSession** | Public entry point. Owns the LineBuffer, Parser, and event dispatch. Feeds tokens in, emits events out. |
| **Parser** | Implements the CommonMark 3-phase block parsing algorithm. Takes complete lines, updates the AST. |
| **ASTNode** | Tree node with vector-based children, type-erased metadata, and text content. No parent pointers -- navigation is handled by the Parser's open block stack. |
| **BlockEvent** | Lightweight event struct (action, type, depth, node pointer). No allocations. Consumers decide what to do with it. |
| **HtmlRenderer** | Stateless HTML renderer. Traverses an AST subtree and produces HTML. Independent of Parser and StreamingSession. |
| **LineBuffer** | Accumulates streaming chunks into complete lines. Handles buffer compaction and size limits. |
| **Scanners** | Pure functions that detect block-level markdown syntax at a given offset. The foundation for all block recognition. |

### Block Types

Markstream recognizes nine CommonMark block types:

| Block | Container? | Description |
|-------|-----------|-------------|
| Document | Yes | Root node, always present |
| BlockQuote | Yes | `>` prefixed content |
| List | Yes | Ordered or unordered list wrapper |
| Item | Yes | Individual list item |
| CodeBlock | No (leaf) | Fenced (`` ``` ``) or indented (4+ spaces) |
| Heading | No (leaf) | ATX (`#`) or setext (`===`/`---`) |
| HtmlBlock | No (leaf) | Raw HTML (7 types per CommonMark) |
| Paragraph | No (leaf) | Default text container |
| ThematicBreak | No (leaf) | `***`, `---`, or `___` |

### Event Lifecycle

Every block goes through a predictable lifecycle:

```
Open  -->  Update (0 or more)  -->  Close
```

- **Open**: A new block has been detected. The `node` pointer is valid and the block's type and metadata are set.
- **Update**: The block received new content (a new line of text). Only emitted if `set_emit_updates(true)` (the default). Useful for live-updating UI as a code block or paragraph grows line by line.
- **Close**: The block is finalized. Its content is immutable from this point forward. Safe to render final HTML, archive, or discard.

## WASM Integration

Markstream treats WebAssembly as a first-class target, not an afterthought. The architecture accounts for the specific constraints of the WASM/JS boundary:

- **Polling mode** exists specifically for WASM. Instead of firing callbacks across the FFI boundary for every event (expensive), events queue internally and are consumed in batches via `pop_events(n)` -- one FFI call returns many events.
- **Minimal allocations in events.** `BlockEvent` is a small struct with no heap allocations (action enum, type enum, depth byte, node pointer). Batches of events can be copied across the boundary efficiently.
- **HtmlRenderer is separate.** Rendering happens on the consumer side, only when needed. The JS layer can choose to render closed blocks to HTML, or use the node pointer to extract content directly -- avoiding unnecessary string copies across the boundary.

## Project Status

Markstream is under active development. The block parser (3-phase CommonMark algorithm) is fully implemented but undergoing a refactor from cmark-style linked lists to vector-based trees for better cache performance.

### What works
- All block-level scanners (~40 tests passing)
- Full 3-phase block parsing algorithm (ATX/setext headings, fenced/indented code, blockquotes, ordered/unordered lists, HTML blocks, thematic breaks, paragraphs)
- HTML rendering of complete ASTs
- LineBuffer for streaming token accumulation
- Lightweight event struct design

### In progress
- AST refactor: ASTNode header uses vector children, but Parser and HtmlRenderer still reference the old linked-list API. See `IMPLEMENTATION_PLAN.md`.
- StreamingSession: declared but mostly unimplemented (event diffing, polling interface, finish/reset)

### Planned
- Complete the vector-based AST transition
- Full StreamingSession implementation with event diffing
- CommonMark spec test harness (~652 examples)
- Inline parsing (emphasis, links, code spans, images)
- WASM build target and JS bindings

## License

TBD
