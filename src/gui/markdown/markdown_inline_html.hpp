#pragma once

#include "gui/markdown/markdown_layout.hpp"
#include "gui/markdown/markdown_style.hpp"

#include <optional>
#include <string>
#include <vector>

namespace remin::markdown {

// Inline HTML/CSS support for the native preview + PDF renderers (NOT the HTML
// export, which stays a verbatim pass-through). This is a deliberate, tiny
// subset — never a browser engine:
//
//   tags:   span (with style="..."), b/strong, i/em, u, s/del, code, mark
//   CSS in style="": color, background-color, font-weight, font-style,
//                   text-decoration, font-size (pt/px/%)
//
// Anything beyond the subset fails safely: the tag/style is ignored, the text
// content is kept, and no input can crash the renderer.

// Parsed declaration style. Every field carries an explicit "set" flag so an
// inline style may only override the subset it actually names.
struct InlineCssStyle {
    bool has_color = false;
    Color color;
    bool has_bg = false;
    Color background;
    int weight = 0;          // 0 = inherit (normal/bold parse to 400/700, or 100..900)
    bool italic_set = false;
    bool italic = false;
    bool underline_set = false;
    bool underline = false;
    bool strike_set = false;
    bool strike = false;
    double size_pt = 0.0;    // absolute override when set (pt/px) — 0 = inherit
    double size_pct = 0.0;   // relative multiplier when set in % (1.2 = 120%)
    std::string font_family; // empty = inherit
};

// Parse a style="..." declaration block ("color:red; background:#fee;").
// Unknown properties and malformed values are ignored (never crash).
[[nodiscard]] InlineCssStyle parse_css_declaration(const std::string& css);

// Combined style for a tag name + its style="..." attribute: the semantic
// defaults (b -> bold, code -> monospace+bg, ...) merged with the CSS
// overrides. `style` resolves the Code background for code/mark decoration.
[[nodiscard]] InlineCssStyle style_from_tag(const std::string& name,
                                            const std::string& css,
                                            const StyleSheet& style);

// One parsed HTML tag (a single "<...>" fragment).
struct HtmlTag {
    std::string name;          // lowercase tag name
    std::string style;         // raw style="..." value
    bool closing = false;      // "</name"
    bool self_closing = false; // trailing "/"
    std::size_t consumed = 0;  // chars consumed from `raw` including '>'
};

// Parse the leading tag of `raw` (which must start at '<'). Returns nullopt
// for comments/doctype/malformed input (fail-safe: callers skip the fragment).
[[nodiscard]] std::optional<HtmlTag> parse_html_tag(const std::string& raw);

// True if `name` is a block-level element. Nesting such an element inside a
// raw HTML block chunk is unsupported (no block layout) — the chunk is dropped.
[[nodiscard]] bool is_block_level_tag(const std::string& name);

// Render a raw HTML block chunk (md4c "type-6" grouping: e.g.
// `<div style="color:blue">Hello <b>world</b></div>\n`) into styled runs. The
// first tag acts as the chunk wrapper; only the inline subset may nest inside.
// Returns false when the chunk cannot be rendered (block-level nesting) so the
// caller keeps the historical "drop" behavior.
[[nodiscard]] bool render_html_block_runs(const std::string& html,
                                          const StyleSheet& style,
                                          const std::string& base_font,
                                          double base_size_pt, Color base_color,
                                          std::vector<StyledText>& out);

} // namespace remin::markdown