#include <gtest/gtest.h>
#include "scanners.hpp"

// ============================================================================
// Character Classification Tests
// ============================================================================

TEST(CharClassification, IsSpace) {
  EXPECT_TRUE(scan::is_space(' '));
  EXPECT_TRUE(scan::is_space('\t'));
  EXPECT_TRUE(scan::is_space('\n'));
  EXPECT_TRUE(scan::is_space('\r'));
  EXPECT_FALSE(scan::is_space('a'));
  EXPECT_FALSE(scan::is_space('0'));
}

TEST(CharClassification, IsSpaceOrTab) {
  EXPECT_TRUE(scan::is_space_or_tab(' '));
  EXPECT_TRUE(scan::is_space_or_tab('\t'));
  EXPECT_FALSE(scan::is_space_or_tab('\n'));
  EXPECT_FALSE(scan::is_space_or_tab('a'));
}

TEST(CharClassification, IsDigit) {
  for (char c = '0'; c <= '9'; c++) {
    EXPECT_TRUE(scan::is_digit(c));
  }
  EXPECT_FALSE(scan::is_digit('a'));
  EXPECT_FALSE(scan::is_digit(' '));
}

TEST(CharClassification, IsAlpha) {
  for (char c = 'a'; c <= 'z'; c++) {
    EXPECT_TRUE(scan::is_alpha(c));
  }
  for (char c = 'A'; c <= 'Z'; c++) {
    EXPECT_TRUE(scan::is_alpha(c));
  }
  EXPECT_FALSE(scan::is_alpha('0'));
  EXPECT_FALSE(scan::is_alpha(' '));
}

// ============================================================================
// Indentation Scanner Tests
// ============================================================================

TEST(Indentation, Spaces) {
  size_t columns;
  EXPECT_EQ(scan::scan_indentation("    text", 0, &columns), 4);
  EXPECT_EQ(columns, 4);
}

TEST(Indentation, Tabs) {
  size_t columns;
  EXPECT_EQ(scan::scan_indentation("\ttext", 0, &columns), 1);
  EXPECT_EQ(columns, 4);
}

TEST(Indentation, Mixed) {
  size_t columns;
  EXPECT_EQ(scan::scan_indentation("  \ttext", 0, &columns), 3);
  EXPECT_EQ(columns, 4); // 2 spaces + tab to next multiple of 4
}

TEST(Indentation, NoIndent) {
  size_t columns;
  EXPECT_EQ(scan::scan_indentation("text", 0, &columns), 0);
  EXPECT_EQ(columns, 0);
}

// ============================================================================
// Blank Line Tests
// ============================================================================

TEST(BlankLine, Empty) {
  EXPECT_TRUE(is_blank_line("", 0));
}

TEST(BlankLine, SpacesOnly) {
  EXPECT_TRUE(is_blank_line("   ", 0));
  EXPECT_TRUE(is_blank_line("\t\t", 0));
  EXPECT_TRUE(is_blank_line("  \t  ", 0));
}

TEST(BlankLine, WithContent) {
  EXPECT_FALSE(is_blank_line("text", 0));
  EXPECT_FALSE(is_blank_line("  text", 0));
}

TEST(BlankLine, WithOffset) {
  EXPECT_TRUE(is_blank_line("text   ", 4));
  EXPECT_FALSE(is_blank_line("text   x", 4));
}

// ============================================================================
// ATX Heading Tests
// ============================================================================

TEST(AtxHeading, ValidHeadings) {
  EXPECT_EQ(scan_atx_heading_start("# heading", 0), 1);
  EXPECT_EQ(scan_atx_heading_start("## heading", 0), 2);
  EXPECT_EQ(scan_atx_heading_start("### heading", 0), 3);
  EXPECT_EQ(scan_atx_heading_start("#### heading", 0), 4);
  EXPECT_EQ(scan_atx_heading_start("##### heading", 0), 5);
  EXPECT_EQ(scan_atx_heading_start("###### heading", 0), 6);
}

TEST(AtxHeading, EmptyHeading) {
  EXPECT_EQ(scan_atx_heading_start("#", 0), 1);
  EXPECT_EQ(scan_atx_heading_start("# ", 0), 1);
}

TEST(AtxHeading, TooManyHashes) {
  // 7 or more hashes is not a heading
  EXPECT_EQ(scan_atx_heading_start("####### heading", 0), 0);
}

TEST(AtxHeading, NoSpaceAfter) {
  // Must have space or end of line after hashes
  EXPECT_EQ(scan_atx_heading_start("#heading", 0), 0);
}

TEST(AtxHeading, WithOffset) {
  EXPECT_EQ(scan_atx_heading_start("  ## heading", 2), 2);
}

TEST(AtxHeading, TrailingHashes) {
  // Returns length to trim from end (hashes + trailing whitespace)
  EXPECT_EQ(scan_atx_heading_end("# heading #"), 1);      // just "#"
  EXPECT_EQ(scan_atx_heading_end("# heading ###"), 3);    // "###"
  EXPECT_EQ(scan_atx_heading_end("# heading ### "), 4);   // "### "
  EXPECT_EQ(scan_atx_heading_end("# heading #  "), 3);    // "#  "
  EXPECT_EQ(scan_atx_heading_end("# heading"), 0);        // no trailing hashes
  EXPECT_EQ(scan_atx_heading_end("# heading#"), 0);       // no space before hash
}

// ============================================================================
// Setext Heading Tests
// ============================================================================

TEST(SetextHeading, Equals) {
  char c;
  EXPECT_GT(scan_setext_heading_line("===", 0, &c), 0);
  EXPECT_EQ(c, '=');
  EXPECT_GT(scan_setext_heading_line("=", 0, &c), 0);
  EXPECT_GT(scan_setext_heading_line("====  ", 0, &c), 0);
}

TEST(SetextHeading, Dashes) {
  char c;
  EXPECT_GT(scan_setext_heading_line("---", 0, &c), 0);
  EXPECT_EQ(c, '-');
  EXPECT_GT(scan_setext_heading_line("-", 0, &c), 0);
  EXPECT_GT(scan_setext_heading_line("----  ", 0, &c), 0);
}

TEST(SetextHeading, Invalid) {
  char c;
  EXPECT_EQ(scan_setext_heading_line("=-=", 0, &c), 0);  // Mixed
  EXPECT_EQ(scan_setext_heading_line("--- text", 0, &c), 0);  // Content after
  EXPECT_EQ(scan_setext_heading_line("text", 0, &c), 0);
}

// ============================================================================
// Code Fence Tests
// ============================================================================

TEST(CodeFence, OpenBackticks) {
  CodeFenceInfo info;
  EXPECT_EQ(scan_open_code_fence("```", 0, &info), 3);
  EXPECT_EQ(info.fence_char, '`');
  EXPECT_EQ(info.fence_length, 3);
  EXPECT_EQ(info.info, "");
}

TEST(CodeFence, OpenTildes) {
  CodeFenceInfo info;
  EXPECT_EQ(scan_open_code_fence("~~~", 0, &info), 3);
  EXPECT_EQ(info.fence_char, '~');
  EXPECT_EQ(info.fence_length, 3);
}

TEST(CodeFence, WithInfo) {
  CodeFenceInfo info;
  EXPECT_EQ(scan_open_code_fence("```python", 0, &info), 3);
  EXPECT_EQ(info.info, "python");

  EXPECT_EQ(scan_open_code_fence("```  javascript  ", 0, &info), 3);
  EXPECT_EQ(info.info, "javascript");
}

TEST(CodeFence, LongerFence) {
  CodeFenceInfo info;
  EXPECT_EQ(scan_open_code_fence("`````", 0, &info), 5);
  EXPECT_EQ(info.fence_length, 5);
}

TEST(CodeFence, TooShort) {
  CodeFenceInfo info;
  EXPECT_EQ(scan_open_code_fence("``", 0, &info), 0);
  EXPECT_EQ(scan_open_code_fence("`", 0, &info), 0);
}

TEST(CodeFence, BacktickInInfo) {
  CodeFenceInfo info;
  // Backtick fences cannot have backticks in info string
  EXPECT_EQ(scan_open_code_fence("```py`thon", 0, &info), 0);
  // Tilde fences can have backticks
  EXPECT_EQ(scan_open_code_fence("~~~py`thon", 0, &info), 3);
}

TEST(CodeFence, Close) {
  EXPECT_EQ(scan_close_code_fence("```", 0, '`', 3), 3);
  EXPECT_EQ(scan_close_code_fence("````", 0, '`', 3), 4);
  EXPECT_EQ(scan_close_code_fence("```  ", 0, '`', 3), 3);

  // Must match fence char
  EXPECT_EQ(scan_close_code_fence("~~~", 0, '`', 3), 0);

  // Must be at least as long as opener
  EXPECT_EQ(scan_close_code_fence("```", 0, '`', 5), 0);

  // No content after
  EXPECT_EQ(scan_close_code_fence("``` text", 0, '`', 3), 0);
}

// ============================================================================
// Thematic Break Tests
// ============================================================================

TEST(ThematicBreak, Asterisks) {
  char c;
  EXPECT_EQ(scan_thematic_break("***", 0, &c), 3);
  EXPECT_EQ(c, '*');
  EXPECT_EQ(scan_thematic_break("*****", 0, &c), 5);
}

TEST(ThematicBreak, Dashes) {
  char c;
  EXPECT_EQ(scan_thematic_break("---", 0, &c), 3);
  EXPECT_EQ(c, '-');
}

TEST(ThematicBreak, Underscores) {
  char c;
  EXPECT_EQ(scan_thematic_break("___", 0, &c), 3);
  EXPECT_EQ(c, '_');
}

TEST(ThematicBreak, WithSpaces) {
  char c;
  EXPECT_EQ(scan_thematic_break("* * *", 0, &c), 3);
  EXPECT_EQ(scan_thematic_break("- - -", 0, &c), 3);
  EXPECT_EQ(scan_thematic_break("*  *  *", 0, &c), 3);
}

TEST(ThematicBreak, Invalid) {
  char c;
  EXPECT_EQ(scan_thematic_break("**", 0, &c), 0);  // Too few
  EXPECT_EQ(scan_thematic_break("*-*", 0, &c), 0);  // Mixed
  EXPECT_EQ(scan_thematic_break("*** text", 0, &c), 0);  // Content after
}

// ============================================================================
// Block Quote Tests
// ============================================================================

TEST(BlockQuote, Basic) {
  EXPECT_EQ(scan_block_quote_start("> text", 0), 1);
  EXPECT_EQ(scan_block_quote_start(">text", 0), 1);
  EXPECT_EQ(scan_block_quote_start(">", 0), 1);
}

TEST(BlockQuote, NoMatch) {
  EXPECT_EQ(scan_block_quote_start("text", 0), 0);
  EXPECT_EQ(scan_block_quote_start("", 0), 0);
}

TEST(BlockQuote, WithOffset) {
  EXPECT_EQ(scan_block_quote_start("  > text", 2), 1);
}

// ============================================================================
// List Marker Tests
// ============================================================================

TEST(ListMarker, BulletDash) {
  ListMarkerInfo info;
  EXPECT_GT(scan_list_marker("- item", 0, &info), 0);
  EXPECT_EQ(info.marker_char, '-');
  EXPECT_FALSE(info.is_ordered);
}

TEST(ListMarker, BulletAsterisk) {
  ListMarkerInfo info;
  EXPECT_GT(scan_list_marker("* item", 0, &info), 0);
  EXPECT_EQ(info.marker_char, '*');
  EXPECT_FALSE(info.is_ordered);
}

TEST(ListMarker, BulletPlus) {
  ListMarkerInfo info;
  EXPECT_GT(scan_list_marker("+ item", 0, &info), 0);
  EXPECT_EQ(info.marker_char, '+');
  EXPECT_FALSE(info.is_ordered);
}

TEST(ListMarker, OrderedPeriod) {
  ListMarkerInfo info;
  EXPECT_GT(scan_list_marker("1. item", 0, &info), 0);
  EXPECT_EQ(info.marker_char, '.');
  EXPECT_TRUE(info.is_ordered);
  EXPECT_EQ(info.start_number, 1);
}

TEST(ListMarker, OrderedParen) {
  ListMarkerInfo info;
  EXPECT_GT(scan_list_marker("1) item", 0, &info), 0);
  EXPECT_EQ(info.marker_char, ')');
  EXPECT_TRUE(info.is_ordered);
  EXPECT_EQ(info.start_number, 1);
}

TEST(ListMarker, OrderedLargeNumber) {
  ListMarkerInfo info;
  EXPECT_GT(scan_list_marker("123456789. item", 0, &info), 0);
  EXPECT_EQ(info.start_number, 123456789);
}

TEST(ListMarker, NoSpaceAfter) {
  ListMarkerInfo info;
  // Must have space after marker
  EXPECT_EQ(scan_list_marker("-item", 0, &info), 0);
  EXPECT_EQ(scan_list_marker("1.item", 0, &info), 0);
}

TEST(ListMarker, EmptyItem) {
  ListMarkerInfo info;
  // Marker at end of line is valid
  EXPECT_GT(scan_list_marker("-", 0, &info), 0);
  EXPECT_GT(scan_list_marker("1.", 0, &info), 0);
}

// ============================================================================
// HTML Block Type Detection Tests
// ============================================================================

TEST(HtmlBlock, Type1Script) {
  EXPECT_EQ(scan_html_block_start("<script>", 0), HtmlBlockType::Type1);
  EXPECT_EQ(scan_html_block_start("<SCRIPT>", 0), HtmlBlockType::Type1);
  EXPECT_EQ(scan_html_block_start("</script>", 0), HtmlBlockType::Type1);
  EXPECT_EQ(scan_html_block_start("<pre>", 0), HtmlBlockType::Type1);
  EXPECT_EQ(scan_html_block_start("<style>", 0), HtmlBlockType::Type1);
  EXPECT_EQ(scan_html_block_start("<textarea>", 0), HtmlBlockType::Type1);
}

TEST(HtmlBlock, Type2Comment) {
  EXPECT_EQ(scan_html_block_start("<!-- comment -->", 0), HtmlBlockType::Type2);
  EXPECT_EQ(scan_html_block_start("<!--", 0), HtmlBlockType::Type2);
}

TEST(HtmlBlock, Type3ProcessingInstruction) {
  EXPECT_EQ(scan_html_block_start("<?xml version=\"1.0\"?>", 0), HtmlBlockType::Type3);
  EXPECT_EQ(scan_html_block_start("<?php", 0), HtmlBlockType::Type3);
}

TEST(HtmlBlock, Type4Declaration) {
  EXPECT_EQ(scan_html_block_start("<!DOCTYPE html>", 0), HtmlBlockType::Type4);
  EXPECT_EQ(scan_html_block_start("<!ENTITY>", 0), HtmlBlockType::Type4);
}

TEST(HtmlBlock, Type5CDATA) {
  EXPECT_EQ(scan_html_block_start("<![CDATA[content]]>", 0), HtmlBlockType::Type5);
}

TEST(HtmlBlock, Type6BlockElements) {
  EXPECT_EQ(scan_html_block_start("<div>", 0), HtmlBlockType::Type6);
  EXPECT_EQ(scan_html_block_start("<p>", 0), HtmlBlockType::Type6);
  EXPECT_EQ(scan_html_block_start("<table>", 0), HtmlBlockType::Type6);
  EXPECT_EQ(scan_html_block_start("</div>", 0), HtmlBlockType::Type6);
  EXPECT_EQ(scan_html_block_start("<h1>", 0), HtmlBlockType::Type6);
}

TEST(HtmlBlock, Type7OtherTags) {
  EXPECT_EQ(scan_html_block_start("<custom-element>", 0), HtmlBlockType::Type7);
  EXPECT_EQ(scan_html_block_start("<span>", 0), HtmlBlockType::Type7);
  EXPECT_EQ(scan_html_block_start("</span>", 0), HtmlBlockType::Type7);
}

TEST(HtmlBlock, NoMatch) {
  EXPECT_EQ(scan_html_block_start("not html", 0), HtmlBlockType::None);
  EXPECT_EQ(scan_html_block_start("< space", 0), HtmlBlockType::None);
}

// ============================================================================
// HTML Block End Detection Tests
// ============================================================================

TEST(HtmlBlockEnd, Type1) {
  EXPECT_TRUE(scan_html_block_end("some text </script> more", 0, HtmlBlockType::Type1));
  EXPECT_TRUE(scan_html_block_end("</SCRIPT>", 0, HtmlBlockType::Type1));
  EXPECT_FALSE(scan_html_block_end("no closing tag", 0, HtmlBlockType::Type1));
}

TEST(HtmlBlockEnd, Type2) {
  EXPECT_TRUE(scan_html_block_end("text --> more", 0, HtmlBlockType::Type2));
  EXPECT_FALSE(scan_html_block_end("no end", 0, HtmlBlockType::Type2));
}

TEST(HtmlBlockEnd, Type3) {
  EXPECT_TRUE(scan_html_block_end("text ?> more", 0, HtmlBlockType::Type3));
  EXPECT_FALSE(scan_html_block_end("no end", 0, HtmlBlockType::Type3));
}

TEST(HtmlBlockEnd, Type4) {
  EXPECT_TRUE(scan_html_block_end("DECLARATION>", 0, HtmlBlockType::Type4));
  EXPECT_FALSE(scan_html_block_end("no end", 0, HtmlBlockType::Type4));
}

TEST(HtmlBlockEnd, Type5) {
  EXPECT_TRUE(scan_html_block_end("content]]>", 0, HtmlBlockType::Type5));
  EXPECT_FALSE(scan_html_block_end("no end", 0, HtmlBlockType::Type5));
}

TEST(HtmlBlockEnd, Type6And7BlankLine) {
  EXPECT_TRUE(scan_html_block_end("", 0, HtmlBlockType::Type6));
  EXPECT_TRUE(scan_html_block_end("   ", 0, HtmlBlockType::Type6));
  EXPECT_FALSE(scan_html_block_end("content", 0, HtmlBlockType::Type6));

  EXPECT_TRUE(scan_html_block_end("", 0, HtmlBlockType::Type7));
  EXPECT_TRUE(scan_html_block_end("\t", 0, HtmlBlockType::Type7));
  EXPECT_FALSE(scan_html_block_end("content", 0, HtmlBlockType::Type7));
}

// ============================================================================
// Link Label Tests
// ============================================================================

TEST(LinkLabel, Basic) {
  EXPECT_GT(scan_link_label("[label]", 0), 0);
  EXPECT_EQ(scan_link_label("[label]", 0), 7);
}

TEST(LinkLabel, Empty) {
  EXPECT_EQ(scan_link_label("[]", 0), 0);  // Empty label not valid
  EXPECT_EQ(scan_link_label("[   ]", 0), 0);  // Whitespace only not valid
}

TEST(LinkLabel, NestedBrackets) {
  EXPECT_EQ(scan_link_label("[lab[el]", 0), 0);  // Nested brackets not allowed
}

TEST(LinkLabel, Escaped) {
  EXPECT_GT(scan_link_label("[lab\\]el]", 0), 0);  // Escaped bracket OK
}
