#pragma once

#include <optional>
#include <string>

namespace remin::markdown {

// Remin Document Style — a small, purposeful stylesheet subset shared by the
// native GTK preview and the Cairo PDF export. Not a browser: no flexbox/drag,
// no selectors beyond the document semantics below.
//
// Renders as CSS-like text:
//
//   document { color:#202124; font-family:sans; font-size:11pt; line-height:1.5; }
//   h1 { font-size:22pt; margin-top:20pt; margin-bottom:10pt; }
//   code { background:#f0f1f3; }
//   blockquote { background:#f6f8fa; border-left:4pt; }
//   page { size:a4; margin:18mm; }
//
// Palette references (@text, @accent, @bg, @surface, @border, @text-muted,
// red/orange/amber/green/blue colors) are resolved against the active palette
// at parse time so they re-theme with dark mode without touching the file.

struct Color {
    float r = 0, g = 0, b = 0;
    float a = 1;
    bool valid = false;  // "not set" (inherit upstream)

    friend bool operator==(const Color&, const Color&) = default;
};

// Semantic palette tokens understood inside stylesheet files. `remin_palette`
// fills them from the active GTK theme so preview/PDF can follow dark mode.
struct ReminPalette {
    Color text = Color{0.13f, 0.15f, 0.17f, 1.0f, true};
    Color text_muted = Color{0.42f, 0.44f, 0.48f, 1.0f, true};
    Color accent = Color{0.25f, 0.32f, 0.95f, 1.0f, true};
    Color bg = Color{1.0f, 1.0f, 1.0f, 1.0f, true};
    Color surface = Color{0.98f, 0.98f, 0.98f, 1.0f, true};
    Color border = Color{0.80f, 0.83f, 0.88f, 1.0f, true};
    Color code_bg = Color{0.95f, 0.95f, 0.97f, 1.0f, true};
    Color quote_bg = Color{0.96f, 0.97f, 0.99f, 1.0f, true};
    Color error = Color{0.72f, 0.15f, 0.16f, 1.0f, true};
    Color warning = Color{0.72f, 0.45f, 0.05f, 1.0f, true};
    Color ok = Color{0.12f, 0.55f, 0.26f, 1.0f, true};
    Color info = Color{0.12f, 0.38f, 0.65f, 1.0f, true};
};

enum class StyleSelector : int {
    Document,
    H1, H2, H3, H4, H5, H6,
    Paragraph,
    Code,       // inline code
    Pre,        // fenced code block
    Blockquote,
    Link,
    List, ListItem,
    Table, TableHeader, TableCell, TableRow,
    Hr,
    Toc, TocTitle, TocLink,
    Image,
    Count
};

struct TextStyle {
    std::string font_family;            // empty = inherit
    double font_size_pt = 0;            // 0 = inherit
    int weight = 0;                     // 0 = inherit (400/700)
    bool italic = false;
    bool italic_set = false;
    bool strike = false;
    bool strike_set = false;
    bool underline = false;
    bool underline_set = false;
    Color color;                        // invalid = inherit
    Color background;                   // invalid = none
};

struct BoxStyle {
    double margin_top_pt = 0;      // always a value; resolution merges
    double margin_bottom_pt = 0;
    double margin_left_pt = 0;
    double margin_right_pt = 0;
    double padding_pt = 0;
    double border_left_width_pt = 0;
    double border_width_pt = 0;
    Color border_color;            // invalid = default border
    Color background;              // invalid = none
    bool any_set = false;
};

struct StyleSheet {
    // Page geometry (PDF). A4 default.
    double page_width_pt = 595.28;
    double page_height_pt = 841.89;
    double page_margin_pt = 56.69;
    std::string page_size = "a4";

    // Document base text style.
    std::string base_font = "sans";
    double base_font_pt = 11.0;
    double line_height = 1.5;
    Color text_color;
    Color bg_color;
    Color hr_color;
    Color link_color;

    TextStyle text[static_cast<int>(StyleSelector::Count)];
    BoxStyle box[static_cast<int>(StyleSelector::Count)];

    [[nodiscard]] std::string font_for(StyleSelector s) const;
    [[nodiscard]] double font_size_for(StyleSelector s) const;
    [[nodiscard]] Color color_for(StyleSelector s) const;
    [[nodiscard]] bool italic_for(StyleSelector s) const;
    [[nodiscard]] bool strike_for(StyleSelector s) const;
    [[nodiscard]] bool underline_for(StyleSelector s) const;
    [[nodiscard]] Color bg_color_for(StyleSelector s) const;
    [[nodiscard]] BoxStyle box_for(StyleSelector s) const;

    [[nodiscard]] double content_width_pt() const {
        return page_width_pt - 2.0 * page_margin_pt;
    }
};

// Builtin default stylesheet. `palette` may be the light or dark palette.
[[nodiscard]] StyleSheet default_style(const ReminPalette& palette);

// Parse a Remin Document Style text on top of `base`. Unknown selectors and
// properties are ignored (never crash). Returns a merged style.
[[nodiscard]] StyleSheet apply_style(const std::string& css,
                                     const StyleSheet& base,
                                     const ReminPalette& palette);

// Quantity helpers ("18mm", "12pt", "1.2") -> pt.
[[nodiscard]] double pt_from_unit(const std::string& value, double fallback);

// Standard palettes.
[[nodiscard]] ReminPalette light_palette();
[[nodiscard]] ReminPalette dark_palette();

} // namespace remin::markdown