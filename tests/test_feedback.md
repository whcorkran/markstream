# Spec Test Feedback

CommonMark spec run: **442/652 passing (67.8%)**, 4 timeouts.

The 210 failures and 4 timeouts fall into distinct, independent bug classes. They are listed below in rough priority order (highest-impact first). Fixing the top three classes (loose lists, reference links, URL normalization) would lift the score by roughly 150 examples.

---

## 1. Loose List Rendering — 43 failures

**Sections:** List items (26), Lists (17)

A list is *loose* when any of its constituent items are separated by blank lines, or when any item directly contains two block-level elements. Loose lists require each item's content to be wrapped in `<p>` tags; tight lists do not. The parser is not promoting items to loose when it should be.

The bug manifests in two related ways:

**A) Blank lines between items not triggering loose mode**
```
Input:    "- a\n- b\n\n- c\n"
Expected: <ul><li><p>a</p></li><li><p>b</p></li><li><p>c</p></li></ul>
Actual:   <ul><li>a</li><li>b</li><li>c</li></ul>
```

**B) Continuation paragraphs inside items not recognized as loose**
```
Input:    "- one\n\n  two\n"
Expected: <ul><li><p>one</p><p>two</p></li></ul>
Actual:   <ul><li>onetwo</li></ul>
```
The blank line + indented continuation is not detected as a second paragraph inside the item; both pieces of text get merged into a single paragraph without `<p>` wrapping. This also causes examples like `- foo\n\n  bar\n` to collapse.

**C) `<li>` wrapping of block children missing newline**
Even in cases where structure is otherwise correct, the renderer emits `<li>foo<ul>...` instead of `<li>foo\n<ul>...`. This causes dozens of near-misses. Examples: #292, #293, #294, #296, #298, #299, #300, #319–#321, #323.

**Root cause:** `finalize()` in `parser.cpp` sets tight/loose via `NODE_LAST_LINE_BLANK` propagation, but the logic is incomplete. See cmark's `blocks.c` `finalize()` for the reference propagation algorithm. The renderer also needs to emit a `\n` after the item's first block child when there are multiple children (`html_renderer.cpp`).

**Affected examples:** #254–#264, #270–#274, #277–#281, #283–#284, #286–#288, #290, #292–#300, #301–#302, #306–#307, #310–#321, #323–#326

---

## 2. Reference-Style Links and Images — ~100 failures

**Sections:** Link reference definitions (21), Links (63), Images (15)

This is the largest single class. The entire reference link system is unimplemented:
- No block-level `[label]: url "title"` definition parsing
- No inline `[text][label]`, `[text][]`, `[label]` resolution
- No case-insensitive, whitespace-normalizing label matching
- No Unicode case folding for labels (e.g. `[ẞ]` matches `[SS]`, example #540)

**Sub-issues within this class:**

**A) Block parser does not recognize link reference definitions**
```
Input:    "[foo]: /url \"title\"\n\n[foo]\n"
Expected: <p><a href="/url" title="title">foo</a></p>
Actual:   <p>[foo]: /url "title"</p><p>[foo]</p>
```
The definition line is treated as a paragraph. Required: a new `NodeType` (or out-of-band map) built during block parsing that collects `label → {url, title}` mappings. Definitions may span multiple lines (url on next line, title across lines). Definition blocks must produce no output themselves (example #207 expects empty output).

**B) Inline parser does not resolve `[text][label]` or `[text][]`**
All reference link forms fall through as literal text.

**C) Empty-bracket collapsed reference `[foo][]` not handled** (examples #553–#556, #566, #584–#587)

**D) Bare shortcut reference `[foo]` not handled** (examples #557–#562, #588–#591)

**E) label matching must be case-insensitive and normalize internal whitespace** (examples #205, #206, #539, #540, #555, #561)

**F) Two timeout cases** (#545 `[bar][foo\!]`, presumably a loop in `][` handling when label contains escape) — these are related hangs, not separate bugs.

**Implementation note:** This requires a two-pass approach (or a deferred-resolution pass): block parsing collects definitions into a `std::unordered_map<string, LinkDef>` with normalized keys, then inline parsing uses the map. The `StreamingSession` needs to expose the map to `HtmlRenderer`, or `HtmlRenderer` must accept it as a parameter.

---

## 3. URL Normalization and Percent-Encoding — ~20 failures

**Sections:** Links, Autolinks, Backslash escapes, Entity references

CommonMark requires URLs in link destinations to be normalized:
- Backslash escapes resolved in URLs: `\*` → `*`, `\(` → `(` (examples #22, #495, #498, #500, #502)
- Backslashes encoded as `%5C` when not an escape: `foo\bar` → `foo%5Cbar` (example #502)
- Entities decoded then re-encoded: `&ouml;` in URL → `%C3%B6` (examples #32, #34)
- Autolink URLs with backslashes percent-encoded: `\[` → `%5C%5B` (example #603)
- Spaces in `<angle bracket>` destinations encoded: `/my uri` → `/my%20uri` (example #489)
- Non-ASCII characters in URLs percent-encoded (example #507 NBSP, example #206 Unicode path)
- Special characters like `"` encoded: `"title"` as dest → `%22title%22` (example #504)

Currently `parse_link_destination` and `parse_inline_link` store the raw URL string verbatim, with no normalization or percent-encoding pass.

**Fix location:** `src/inline.cpp` — `parse_link_destination()` and `parse_inline_link()`. Needs a `normalize_url()` helper that: (1) decodes backslash escapes, (2) decodes HTML entities, (3) percent-encodes characters that must be encoded in a URL context.

---

## 4. ATX Heading Trailing Content Stripping — 5 failures

**Section:** ATX headings (#67, #71, #72, #73, #79)

ATX headings may have an optional trailing sequence of `#` characters (preceded by a space or spaces) that should be stripped. Trailing spaces must also be stripped.

```
Input:    "## foo ##\n"
Expected: <h2>foo</h2>
Actual:   <h2>foo ##</h2>

Input:    "### foo ###     \n"
Expected: <h3>foo</h3>
Actual:   <h3>foo ###     </h3>
```

The existing `chop_trailing_hashtags()` in `parser.cpp` is not being called, is broken, or does not handle the leading-space requirement before the closing `#` sequence.

**Fix location:** `parser.cpp` — `chop_trailing_hashtags()` and its call site in `try_atx_heading()`. Per spec: strip optional spaces, then optional `#*`, then optional spaces, from the right — but only if the `#` sequence is preceded by a space (or is the entire content).

---

## 5. Inline HTML Tag Validation — ~8 failures + 2 timeouts

**Sections:** Raw HTML (#615, #616, #619–#622, #624, #629, #632)

The `is_inline_html_tag()` function in `src/inline.cpp` accepts some malformed tags as valid HTML and rejects/mishandles others.

**Specific bugs:**

**A) Multiline tag attribute parsing** — A tag split across lines (e.g. `<b2\ndata="foo" >`) should be matched as inline HTML; currently the `<...>` scanner stops at `>` scanning a single line and misses the second line. (example #615)

**B) Invalid attribute syntax accepted** — Tags with `*` or `#` in attribute names (example #619), adjacent attributes without whitespace (example #622), and closing tags with attributes (example #624) should be rejected (output as literal `&lt;...&gt;`).

**C) Timeout in `< a>` parsing** — example #621 (`< a><\nfoo>...`) causes an infinite loop. The leading space after `<` should make it an invalid tag, but the scanner apparently loops.

**D) Timeout in `<!-- ... -->` comment with `--` inside** — example #625 (`foo <!-- this is a --\ncomment...`). CommonMark requires that HTML comments not contain `--` except as `-->`. The comment scanner likely has a loop when encountering `--` mid-comment without immediately seeing `>`.

**E) CDATA passthrough** — `<![CDATA[...]]>` content should pass through unescaped; currently the content is being HTML-escaped (example #629).

**F) Backslash in closing tag** — `<a href="\"">` should be rejected as invalid (example #632).

**Fix location:** `src/inline.cpp` — `is_inline_html_tag()` and the `<` branch in `parse_inlines()`. The multiline issue requires the scanner to operate on a pre-joined multi-line string or accept `\n` in tag content where the spec permits it.

---

## 6. Single-Quote (`'`) HTML Escaping — ~5 failures

**Sections:** Backslash escapes (#12), Link reference definitions (#197), Textual content (#650), Hard line breaks (indirect)

The block-level `HtmlRenderer::escape_html()` escapes `'` as `&#39;` but the spec requires it to be passed through literally in paragraph/body contexts (only `&`, `<`, `>`, `"` need escaping in body text per HTML5).

```
Input:    "hello $.;'there\n"
Expected: <p>hello $.;'there</p>
Actual:   <p>hello $.;&#39;there</p>
```

Note: `&#39;` is correct for *attribute values* (used by the inline renderer for URL/title attributes), but wrong for body text.

**Fix location:** `src/html_renderer.cpp` — `HtmlRenderer::escape_html()`. Remove `'` → `&#39;` from the body-text escaping path. The inline renderer's separate `escape_html()` in `src/inline.cpp` already correctly escapes `'` for attribute contexts and should remain unchanged.

---

## 7. Thematic Breaks Inside List Items — 3 failures

**Section:** Thematic breaks (#57, #60), List items (#61)

A thematic break inside a list item should interrupt and close the list item (and the list), not be consumed as item content.

```
Input:    "- foo\n***\n- bar\n"
Expected: <ul><li>foo</li></ul><hr /><ul><li>bar</li></ul>
Actual:   <ul><li>foo<hr /></li><li>bar</li></ul>
```

The spec rule: a thematic break line can interrupt a list item (it terminates the item, then starts a new block). The parser currently treats `***` as content of the open list item.

**Fix location:** `parser.cpp` — Phase 1 (`check_open_blocks`) continuation check for list items. When the current line matches `scan_thematic_break`, the item's continuation should fail, causing the item (and possibly the list) to be finalized before the thematic break is opened.

---

## 8. Setext Heading / Thematic Break Inside List — 2 failures

**Section:** Setext headings (#94, #99)

Similar to class 7: a setext heading underline (`---`, `=====`) that appears after a list item should not be consumed as a setext underline for that item's text, but should instead close the list and become a thematic break or heading.

```
Input:    "- Foo\n---\n"
Expected: <ul><li>Foo</li></ul><hr />
Actual:   <ul><li>Foo<hr /></li></ul>
```

**Fix location:** `parser.cpp` — Phase 2 setext heading detection (`try_setext_heading`). A setext underline should only convert the *immediately preceding paragraph* to a heading. If the preceding block is a list item, the underline should not be treated as a setext underline.

---

## 9. Backslash Escapes in Fenced Code Info Strings — 1 failure

**Section:** Backslash escapes (#24)

The info string (language tag) of a fenced code block should have backslash escapes resolved before being rendered as the `class="language-..."` attribute.

```
Input:    "``` foo\\+bar\nfoo\n```\n"
Expected: <pre><code class="language-foo+bar">
Actual:   <pre><code class="language-foo\\+bar">
```

**Fix location:** `src/html_renderer.cpp` — when rendering the `language-` class from `CodeData`, apply backslash escape resolution to the info string before output.

---

## 10. Trailing Whitespace Stripping — 5 failures

**Sections:** ATX headings (#67), Setext headings (#82, #89), Indented code blocks (#118), Hard line breaks (#635, #645, #647)

Multiple places where trailing spaces or tabs are not stripped when they should be:

- **ATX headings:** trailing spaces after `#` markers not stripped (partially overlaps class 4)
- **Setext headings:** trailing tab `\t` on continuation line not stripped (example #82), trailing two spaces not stripped when there is no hard break intent (example #89)
- **Indented code blocks:** `finalize()` trims trailing blank lines but not trailing spaces on the last content line (example #118: `    foo  ` should preserve the trailing spaces — actually this one the spec says preserve them, which means the trim in `finalize()` is stripping too aggressively)
- **Hard line breaks:** a trailing-space hard break at end of paragraph (`foo  \n` with nothing after) should not produce `<br />`; trailing spaces on the last line of a paragraph are stripped (#645). Also `### foo  \n` should strip trailing spaces and not produce a `<br />` (#647)

---

## 11. Entity Decoding Completeness — 5 failures

**Section:** Entity and numeric character references (#25, #26, #32, #34)

**A) Named entity table incomplete** — Only a subset of HTML5 named entities are decoded. The spec requires all ~2000 HTML5 named entities. Example #25 fails because `&nbsp;`, `&copy;`, `&AElig;`, `&frac34;`, `&HilbertSpace;`, etc. are not in the entity table.

**B) Codepoint `U+0000` replacement** — Numeric reference `&#0;` must be replaced with the Unicode replacement character `U+FFFD` (`\xEF\xBF\xBD`), not with the null byte `\x00`. Example #26.

**C) Entities in link URLs** — `&ouml;` in a link destination should be decoded to `ö` then percent-encoded to `%C3%B6` (examples #32, #34). This overlaps with class 3 (URL normalization).

**Fix location:** `src/inline.cpp` — `decode_entity()`. Expand the named entity lookup table to the full HTML5 set (generate from https://html.spec.whatwg.org/entities.json or embed cmark's entity table). Add `U+0000` → `U+FFFD` replacement.

---

## 12. Inline Code Span Priority Over Links — 2 failures

**Section:** Code spans (#342), Links (#525)

Backtick code spans have higher precedence than link brackets. A `[` that is followed by a backtick-delimited code span (before the closing `]`) should not be parsed as a link opener.

```
Input:    "[not a `link](/foo`)\n"
Expected: <p>[not a <code>link](/foo</code>)</p>
Actual:   <p><a href="/foo`">not a `link</a></p>
```

The inline parser processes the `[` before it sees the backtick, eagerly treating everything up to `]` as a link label. The fix: during link-label scanning, code spans inside the label must be respected and cannot be "broken into" by `]`.

**Fix location:** `src/inline.cpp` — `parse_link_label_end()`. When scanning for the closing `]`, skip over any backtick-delimited code span runs encountered within the label.

---

## 13. Non-ASCII Flanking Rules for Emphasis — 3 failures

**Section:** Emphasis and strong emphasis (#353, #354)

The CommonMark spec uses Unicode categories for flanking delimiter classification: a delimiter is left-flanking only if it is not followed by a Unicode whitespace character, and not followed by a Unicode punctuation character unless preceded by one. The current implementation uses `isspace()` and `ispunct()`, which are ASCII-only.

```
Input:    "*\xa0a\xa0*\n"   (NBSP around content)
Expected: <p>*\xa0a\xa0*</p>   (not emphasis, NBSP is Unicode whitespace)
Actual:   <p><em>\xa0a\xa0</em></p>
```

**Fix location:** `src/inline.cpp` — `is_left_flanking()` / `is_right_flanking()`. Replace ASCII `isspace` checks with a Unicode whitespace test (at minimum: check for `\xc2\xa0` i.e. NBSP in UTF-8), and extend `is_ascii_punct` to cover Unicode punctuation categories for the characters that commonly appear.

---

## 14. Emphasis Delimiter Run Consumption — ~8 failures

**Section:** Emphasis and strong emphasis (#409, #414–#417, #427, #431, #464–#468, #470)

When a delimiter run of length N can open or close emphasis, it must be consumed in chunks of 1 or 2 (matching openers to closers). Runs longer than 2 that don't divide evenly leave remainders. The current `process_emphasis()` implementation does not correctly handle runs >2, nor does it handle mixed `*`/`_` openers within the same run.

```
Input:    "***foo***\n"
Expected: <p><em><strong>foo</strong></em></p>
Actual:   <p>*<strong>foo</strong>*</p>

Input:    "****foo****\n"
Expected: <p><strong><strong>foo</strong></strong></p>
Actual:   <p>**<strong>foo</strong>**</p>
```

The algorithm should match 2 delimiters for `<strong>`, then 1 for `<em>`, consuming all 3 from each side. Currently when the opener run is 3 and closer is 3, only 2 are matched (producing `<strong>`) and the remaining 1 on each side is left as literal `*`.

**Fix location:** `src/inline.cpp` — `process_emphasis()`. Re-examine the loop that matches opener/closer pairs. After matching a pair, the remaining run length must be updated and the match loop must continue for the same closer until the run is fully consumed.

---

## 15. Miscellaneous Small Bugs

### Link parsing edge cases — ~5 failures

- **Space before `(`:** `[link] (/uri)` (space between label and paren) must NOT be a link (example #511). Currently accepted.
- **Empty destination `()`:** `[link]()` must produce `<a href="">` (example #485, #487, #567). Currently rejected.
- **Nested links rejected too aggressively:** `[foo [bar](/uri)](/uri)` — the outer link should be rejected because it contains an inner link, but `[foo` should still render as literal text with the inner link intact (example #518).
- **Balanced paren depth in destinations:** `foo(and(bar))` is a valid destination (depth 2), but parsing stops too early (example #496). The current paren counter has an off-by-one.
- **Inline HTML inside link label takes priority:** `[foo <bar attr="](baz)">` — the `"` inside the HTML attribute prevents `]` from closing the label (example #524).

### Autolinks — 2 failures

- **Space inside `<...>` rejects autolink:** `<https://foo.bar/baz bim>` should not be autolink (correct), but the output should be `&lt;https://foo.bar/baz bim&gt;` not raw `<...>` (example #602). The scanner rejects it but the fallback does not HTML-escape the `<`.
- **Short scheme rejected:** `<m:abc>` — a scheme of length 1 should be rejected as an autolink (example #609). Currently accepted.

### Block quote lazy continuation — 1 failure

- **Indented continuation in blockquote:** `> foo\n    - bar\n` — the 4-space-indented continuation should be a lazy paragraph continuation (not an indented code block), because it's inside a block quote context (example #238). Currently parsed as indented code.

### Paragraph trailing space in hard breaks — 2 failures

- Trailing spaces that constitute a hard break are stripped when they are the last content of the document (examples #645, #647). The spec says: a hard line break requires content *after* it on the next line; a trailing `  ` at document end is just trailing whitespace, not a break.

### Indented code block trailing blank lines — 1 failure

- Blank lines at the end of an indented code block are included in the output (example #117). `finalize()` trims them for indented code but the trim is off by one iteration or doesn't handle leading blank lines before content properly.

---

## Summary Table

| Class | Failures | Effort |
|---|---|---|
| 1. Loose list rendering | 43 | Medium — tight/loose propagation in `parser.cpp` + renderer newlines |
| 2. Reference-style links/images | ~100 | High — new block node type, inline resolver, label normalization |
| 3. URL normalization / percent-encoding | ~20 | Medium — new `normalize_url()` in `inline.cpp` |
| 4. ATX heading trailing `#` strip | 5 | Low — fix `chop_trailing_hashtags()` |
| 5. Inline HTML tag validation + timeouts | ~10 | Medium — rewrite tag validator, fix 2 loops |
| 6. Single-quote escaping in body text | ~5 | Trivial — remove `'` from body `escape_html()` |
| 7–8. Thematic break / setext inside list | 5 | Low-Medium — interruption logic in Phase 1 |
| 9. Backslash in code fence info string | 1 | Trivial |
| 10. Trailing whitespace stripping | 5 | Low |
| 11. Entity table completeness + U+0000 | 5 | Low (embed full table) |
| 12. Code span priority over links | 2 | Low — skip code spans in `parse_link_label_end()` |
| 13. Non-ASCII flanking for emphasis | 3 | Low — extend Unicode checks |
| 14. Emphasis run consumption >2 | ~8 | Medium — fix `process_emphasis()` loop |
| 15. Misc small bugs | ~10 | Low each |

**Recommended fix order:** 6 (trivial) → 4 (low) → 9 (trivial) → 10 (low) → 12 (low) → 14 (medium) → 1 (medium) → 3 (medium) → 5 (medium) → 2 (high).
