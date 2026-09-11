#pragma once

#include "gui/markdown/markdown_ast.hpp"
#include "gui/markdown/markdown_style.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace remin::markdown {

// Layout Engine: MarkdownAst + StyleSheet -> ordered flow of Blocks with
// measured Pango heights. The same flow feeds the GTK preview (continuous,
// page_height_pt == 0) and the Cairo PDF exporter (paginated). Text is stored
// as styled runs; the shared draw layer wraps them again with the real cairo
// context so preview and PDF break lines identically.

struct StyledText {
    std::string text;
    std::string font_family;
    double size_pt = 11.0;
    int weight = 400;
    bool italic = false;
    bool strike = false;
    bool underline = false;
    Color color;
    Color background;               // inline code background (invalid = none)
    bool is_link = false;
    std::string href;               // link destination ("" = plain)
    std::string anchor;             // fragment target, e.g. "intro" ("" = none)
};

struct Block {
    enum class Kind {
        P, Heading, Quote, Code,
        UlItem, OlItem, TaskItem,
        Table, Hr, Image, TocRow,
        PageBreak,               // hard page boundary in exported documents
    };
    Kind kind = Kind::P;

    double y = 0.0;             // top in the flow (margins included)
    double height = 0.0;
    double baseline = 0.0;      // offset from block top to first text baseline
    double margin_before = 0.0;
    double margin_after = 0.0;
    int level = 0;              // heading level / nesting

    std::vector<StyledText> run;  // inline text of the block

    // Block visuals
    Color bg;              bool has_bg = false;
    Color border;          double border_left_w = 0.0;
    Color outer_border;    double border_w = 0.0;
    Color color;           // hr stroke color
    double padding = 0.0;
    double content_width = 0.0;   // wrapping width actually used

    // Code block
    bool is_code = false;
    std::string code_lang;

    // List marker + hanging-indent width (pt, relative to block origin)
    std::string marker;            // "• ", "1. ", "☐ ", "☑ "
    double indent_pt = 0.0;

    // Image
    bool has_image = false;
    std::filesystem::path image_path;
    double image_w = 0.0, image_h = 0.0;

    // Table (cells laid out; col widths share content_width)
    struct Cell {
        std::vector<StyledText> run;
        double height = 0.0;
        CellAlign align = CellAlign::None;
        bool header = false;
    };
    struct Row {
        std::vector<Cell> cells;
        double height = 0.0;
    };
    bool is_table = false;
    std::vector<Row> rows;
    std::vector<double> col_widths;

    // Toc
    double toc_indent = 0.0;
    std::string toc_number;
    std::string toc_text;
    std::string toc_anchor;
    bool toc_has_page = false;   // PDF: page number shown in the leader
    int toc_page = 0;
    double page_num_w = 0.0;     // measured width of the trailing page number

    // Anchor for hyperlink hit-testing
    double link_x = 0, link_y = 0, link_w = 0, link_h = 0;
    std::string link_href;
    bool has_link_hit = false;
};

struct LayoutResult {
    std::vector<Block> blocks;
    double total_height = 0.0;

    // Blocks that visually belong together with the following block
    // (heading keep-with-next) — the paginator uses this.
    bool block_keeps_with_next(const Block& b) const;
};

// Resolves an image reference to a physical path (may be empty for remote srcs).
using ImageResolver = std::function<std::optional<std::filesystem::path>(const std::string&)>;

// Resolves a heading anchor to its page number (1-based) for the PDF TOC.
using TocPageResolver = std::function<std::optional<int>(const std::string& anchor)>;

// True if a raw HTML block source is a page-break marker, i.e. a block-level
// element whose inline style requests a hard break:
//   <div style="page-break-after: always"></div>
//   <div style="break-after: page"></div>
//   <section style="page-break-before: always"></section>
[[nodiscard]] bool is_page_break_marker(const std::string& html_block_source);

// Layout the whole document into a continuous flow. `note_dir` unused in favor
// of the explicit resolver. `page_height` unused here (pagination lives in the
// exporter so preview/layout stay one shared code path).
[[nodiscard]] LayoutResult layout_document(const MarkdownAst& ast,
                                           const StyleSheet& style,
                                           double content_width_pt,
                                           const ImageResolver& resolve_image,
                                           const TocPageResolver& toc_pages = {});

} // namespace remin::markdown