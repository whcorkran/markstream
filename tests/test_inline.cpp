#include "inline.hpp"
#include <gtest/gtest.h>

TEST(Inline, EmphasisAndStrong) {
  EXPECT_EQ(render_inlines_html("a *b* c"), "a <em>b</em> c");
  EXPECT_EQ(render_inlines_html("a **b** c"), "a <strong>b</strong> c");
}

TEST(Inline, CodeSpan) {
  EXPECT_EQ(render_inlines_html("Use `x < y` now"),
            "Use <code>x &lt; y</code> now");
}

TEST(Inline, EscapesAndEntities) {
  EXPECT_EQ(render_inlines_html("\\*not em* &amp; &#35;"), "*not em* &amp; #");
}

TEST(Inline, InlineLinkAndImage) {
  EXPECT_EQ(render_inlines_html("[x](https://a.test \"t\")"),
            "<a href=\"https://a.test\" title=\"t\">x</a>");
  EXPECT_EQ(render_inlines_html("![alt](img.png)"),
            "<img src=\"img.png\" alt=\"alt\" />");
}

TEST(Inline, AutolinkAndHtmlInline) {
  EXPECT_EQ(render_inlines_html("<https://a.test>"),
            "<a href=\"https://a.test\">https://a.test</a>");
  EXPECT_EQ(render_inlines_html("<em>x</em>"), "<em>x</em>");
}

TEST(Inline, SoftAndHardBreak) {
  EXPECT_EQ(render_inlines_html("a\nb"), "a\nb");
  EXPECT_EQ(render_inlines_html("a  \nb"), "a<br />\nb");
}
