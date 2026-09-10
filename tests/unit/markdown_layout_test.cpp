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

    // --- [[TOC]] expands to toc rows with anchors ---
    {
        const auto ast = MarkdownAst::parse("[[TOC]]\n\n# Intro\n## Deep\n");
        const auto r = layout_document(ast, style, content_w, {});
        bool saw_toc = false;
        for (const auto& b : r.blocks)
            if (b.kind == Block::Kind::TocRow && b.toc_anchor == "intro") saw_toc = true;
        check(saw_toc, "toc row with anchor");
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