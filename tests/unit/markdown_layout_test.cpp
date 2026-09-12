// Unit tests for the layout engine and the Cairo PDF exporter. Both code paths
// are pure Pango/Cairo — no Gtk display is required (72-dpi headless context).
#include "gui/markdown/markdown_layout.hpp"
#include "gui/markdown/markdown_pango.hpp"
#include "gui/markdown/markdown_pdf_export.hpp"
#include "gui/markdown/markdown_style.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <pangomm.h>
#include <pangomm/init.h>

using namespace remin::markdown;

namespace {

int fails = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        ++fails;
        std::cerr << "FAIL: " << msg << "\n";
    }
}

} // namespace

int main() {
    // Headless pangomm: initialize the wrap tables before PangoCairoFontMap is
    // resolved so measure_context() does not deref an unwrapped object.
    Pango::init();

    const StyleSheet style = default_style(light_palette());
    const double content_w = style.content_width_pt();

    // --- measure_runs: identical at any width; width grows with text ---
    {
        StyledText t{"The quick brown fox", "", 11.0, 400, false, false, false, {}, {}, false, "", ""};
        const auto m1 = measure_runs({t}, content_w, style);
        check(m1.width > 0, "measured width positive");
        const auto m_wide = measure_runs({t}, content_w * 2, style);
        check(m_wide.width == m1.width, "no-wrap measure width stable");
        check(m_wide.height > 0, "measured height positive");
    }

    // --- layout_document produces a monotonic flow ---
    {
        const auto ast = MarkdownAst::parse(
            "# Title\n\nSome paragraph text.\n\n- one\n- two\n\n"
            "```cpp\nint main(){}\n```\n\n---\n");
        const auto result = layout_document(ast, style, content_w, {});
        check(!result.blocks.empty(), "blocks produced");
        check(result.total_height > 0, "total height positive");
        double y = 0;
        for (const auto& b : result.blocks) {
            check(b.y >= y - 1e-6, "block y monotonic");
            check(b.height > 0, "block height positive");
            check(b.y + b.height <= result.total_height + 1e-6, "block within flow");
            y = b.y;
        }
        bool saw_heading = false, saw_quoteish = false, saw_hr = false;
        for (const auto& b : result.blocks) {
            saw_heading = saw_heading || b.kind == Block::Kind::Heading;
            saw_hr = saw_hr || b.kind == Block::Kind::Hr;
        }
        check(saw_heading, "heading block present");
        check(saw_hr, "thematic break block present");
    }

    // --- list markers keep text inside content width ---
    {
        const auto ast = MarkdownAst::parse("- first item\n- second item\n");
        const auto r = layout_document(ast, style, content_w, {});
        check(r.blocks.size() == 2, "two list items");
        for (const auto& b : r.blocks) {
            check(b.kind == Block::Kind::UlItem, "unordered item");
        }
    }

    // --- table columns sized and cells populated ---
    {
        const auto ast = MarkdownAst::parse("| a | b |\n|---|---|\n| 1 | 2 |\n");
        const auto r = layout_document(ast, style, content_w, {});
        bool saw_table = false, saw_header = false;
        for (const auto& b : r.blocks) {
            if (b.kind == Block::Kind::Table) {
                saw_table = true;
                check(b.rows.size() == 2, "two table rows");
                check(b.col_widths.size() == 2, "two columns");
                if (!b.rows.empty()) {
                    // First row is the header row; cells carry header=true.
                    for (const auto& cell : b.rows[0].cells)
                        if (cell.header) saw_header = true;
                }
            }
        }
        check(saw_table, "table block present");
        check(saw_header, "header cells flagged");
    }

    // --- inline code spans emit monospace runs: md4c stores the code body in
    // --- node.text with no child Text nodes (regression) ---
    {
        const auto ast = MarkdownAst::parse("subnet `10.0.0.0/24` ok\n");
        const auto r = layout_document(ast, style, content_w, {});
        bool saw_code = false, saw_plain = false;
        for (const auto& b : r.blocks)
            for (const auto& run : b.run) {
                if (run.font_family == "monospace" &&
                    run.text.find("10.0.0.0/24") != std::string::npos)
                    saw_code = true;
                if (run.text.find("ok") != std::string::npos) saw_plain = true;
            }
        check(saw_code, "inline code span emitted as monospace run");
        check(saw_plain, "surrounding text still emitted");
    }

    // --- inline code inside table cells uses the same RunBuilder path ---
    {
        const auto ast = MarkdownAst::parse("| A | B |\n|---|---|\n| `10.0.0.0/24` | x |\n");
        const auto r = layout_document(ast, style, content_w, {});
        bool saw_code = false;
        for (const auto& b : r.blocks)
            for (const auto& row : b.rows)
                for (const auto& cell : row.cells)
                    for (const auto& run : cell.run)
                        if (run.font_family == "monospace" &&
                            run.text.find("10.0.0.0/24") != std::string::npos)
                            saw_code = true;
        check(saw_code, "inline code span in table cell");
    }

    // --- table column min width honors the widest unbreakable word, and a
    // --- overflowing table stays left-aligned (no phantom centering band) ---
    {
        const auto ast = MarkdownAst::parse(
            "| Severity | Risk |\n|---|---|\n| High | RCE |\n");
        const auto r = layout_document(ast, style, 40.0, {});
        const Block* table = nullptr;
        int sev_col = -1;
        for (const auto& b : r.blocks)
            if (b.kind == Block::Kind::Table) {
                table = &b;
                if (!b.rows.empty())
                    for (std::size_t c = 0; c < b.rows.front().cells.size(); ++c)
                        for (const auto& run : b.rows.front().cells[c].run)
                            if (run.text.find("Severity") != std::string::npos)
                                sev_col = static_cast<int>(c);
            }
        check(table, "table present for min-width test");
        if (table && sev_col >= 0) {
            StyledText word{"Severity", "", 11.0, 400, false, false, false,
                            {}, {}, false, "", ""};
            const auto m = measure_runs({word}, -1.0, style);
            check(table->col_widths[sev_col] >= m.width + 2.0 * table->cell_pad,
                  "column at least widest word + cell padding");
            check(table->table_center_x == 0.0, "overflowing table not centered");
        }
    }

    // --- [[TOC]] expands to toc rows with anchors ---
    {
        const auto ast = MarkdownAst::parse("[[TOC]]\n\n# Intro\n## Deep\n");
        const auto r = layout_document(ast, style, content_w, {});
        bool saw_toc = false;
        for (const auto& b : r.blocks)
            if (b.kind == Block::Kind::TocRow && b.toc_anchor == "intro") saw_toc = true;
        check(saw_toc, "toc row with anchor");
    }

    // --- is_page_break_marker: block-level elements with break CSS ---
    {
        check(is_page_break_marker("<div style=\"page-break-after: always;\"></div>"),
              "page-break-after always");
        check(is_page_break_marker("<div style=\"break-after: page;\"></div>"),
              "break-after page");
        check(is_page_break_marker("<section style=\"page-break-before: always\"></section>"),
              "page-break-before always");
        check(is_page_break_marker("<p style=\"break-before: page\">.</p>"),
              "break-before page");
        check(!is_page_break_marker("<div style=\"color: red;\"></div>"),
              "non-break css is not a marker");
        check(!is_page_break_marker("<div></div>"), "plain div not a marker");
        check(!is_page_break_marker("<table></table>"), "unknown tag not a marker");
        check(!is_page_break_marker("not html at all"), "plain text not a marker");
        check(!is_page_break_marker(""), "empty not a marker");
    }

    // --- HtmlBlock page-break markers become PageBreak blocks; non-break ---
    // --- raw HTML chunks render with the inline subset (design Q3, 2026-09-12) ---
    {
        const auto ast = MarkdownAst::parse(
            "Before\n\n"
            "<div style=\"page-break-after: always;\"></div>\n\n"
            "After\n\n"
            "<div class=\"custom\">ignored</div>\n");
        const auto r = layout_document(ast, style, content_w, {});
        bool saw_break = false;
        bool saw_custom_html = false;
        for (const auto& b : r.blocks) {
            if (b.kind == Block::Kind::PageBreak) saw_break = true;
            for (const auto& run : b.run)
                if (run.text.find("ignored") != std::string::npos) saw_custom_html = true;
        }
        check(saw_break, "page-break marker produced a PageBreak block");
        check(saw_custom_html, "non-break raw html chunk kept its text");
    }

    // --- TOC rows receive page numbers via the resolver; title width is ---
    // --- not stolen by the trailing page-number run ---
    {
        const auto ast = MarkdownAst::parse("[[TOC]]\n\n# Alpha\n\n## Beta\n");
        const TocPageResolver resolver = [](const std::string& a) -> std::optional<int> {
            if (a == "alpha") return 2;
            if (a == "beta") return 5;
            return std::nullopt;
        };
        const auto r = layout_document(ast, style, content_w, {}, resolver);
        bool alpha_ok = false, beta_ok = false;
        for (const auto& b : r.blocks) {
            if (b.kind != Block::Kind::TocRow) continue;
            if (b.toc_anchor == "alpha" && b.toc_has_page && b.toc_page == 2) alpha_ok = true;
            if (b.toc_anchor == "beta" && b.toc_has_page && b.toc_page == 5) beta_ok = true;
        }
        check(alpha_ok, "toc alpha got page 2");
        check(beta_ok, "toc beta got page 5");
    }

    // --- paginate_blocks: PageBreak forces a hard boundary and is never ---
    // --- part of any slice (no empty pages) ---
    {
        const auto ast = MarkdownAst::parse(
            "# A\n\nbody one\n\n"
            "<div style=\"page-break-after: always;\"></div>\n\n"
            "# B\n\nbody two\n\n"
            "<div style=\"page-break-after: always;\"></div>\n\n"
            "# C\n\nbody three\n");
        const auto r = layout_document(ast, style, content_w, {});
        const double page_h = 120.0;  // tiny page: forces multiple pages anyway
        const auto pages = paginate_blocks(r.blocks, page_h);
        check(pages.size() >= 2, "page breaks produce multiple pages");

        // markers must not appear inside any slice
        bool marker_in_slice = false;
        for (const auto& p : pages)
            for (std::size_t i = p.first; i <= p.last && i < r.blocks.size(); ++i)
                if (r.blocks[i].kind == Block::Kind::PageBreak) marker_in_slice = true;
        check(!marker_in_slice, "page-break markers excluded from all slices");
    }

    // --- paginate_blocks: leading PageBreak does not create an empty page ---
    {
        const auto ast = MarkdownAst::parse(
            "<div style=\"break-after: page;\"></div>\n\n"
            "# Only\n");
        const auto r = layout_document(ast, style, content_w, {});
        const auto pages = paginate_blocks(r.blocks, 500.0);
        check(pages.size() == 1, "leading marker does not create an empty page");
        check(pages[0].first <= pages[0].last &&
                  pages[0].last < r.blocks.size() &&
                  r.blocks[pages[0].first].kind != Block::Kind::PageBreak,
              "first slice starts on real content");
    }

    // --- export_pdf writes a real PDF (magic header + pages) ---
    {
        const auto ast = MarkdownAst::parse(
            "# Quarterly Report\n\nSome **bold** body text covering a line or two.\n\n"
            "- item a\n- item b\n\nmore *text* here\n\n"
            "## Deep\n\nLast paragraph.\n");
        char tmpl[] = "/tmp/remin_pdf_test_XXXXXX";
        char* dirp = mkdtemp(tmpl);
        check(dirp != nullptr, "mkdtemp");
        const std::filesystem::path out =
            std::filesystem::path(dirp) / "report.pdf";

        PdfPageMeta meta;
        meta.title = "Quarterly Report";
        const bool ok = export_pdf(ast, style, {}, out, meta);
        check(ok, "export_pdf succeeded");
        check(std::filesystem::exists(out), "pdf file exists");
        std::ifstream f(out, std::ios::binary);
        std::string magic(5, '\0');
        f.read(magic.data(), 5);
        check(magic == "%PDF-", "pdf magic header");
        check(std::filesystem::file_size(out) > 1000, "pdf has real content");
        std::string tail(1024, '\0');
        f.seekg(-1024, std::ios::end);
        f.read(tail.data(), 1024);
        check(tail.find("%%EOF") != std::string::npos, "pdf trailer eof");

        std::filesystem::remove_all(dirp);
    }

    // --- export to an unwritable path fails gracefully ---
    {
        const auto ast = MarkdownAst::parse("# x\n");
        check(!export_pdf(ast, style, {}, std::filesystem::path("/nonexistent-dir/x.pdf"), {}),
              "export to unwritable path returns false");
    }

    if (fails == 0) {
        std::cout << "markdown_layout_test: ALL PASS\n";
        return 0;
    }
    std::cerr << "markdown_layout_test: " << fails << " failure(s)\n";
    return 1;
}