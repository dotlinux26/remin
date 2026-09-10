// Unit tests for the MarkdownDocument model, the HTML renderer, the stylesheet
// engine and the builtin CSS. Pure subjects — no GTK widgets are created.
#include "gui/markdown/markdown_css.hpp"
#include "gui/markdown/markdown_document.hpp"
#include "gui/markdown/markdown_html.hpp"
#include "gui/markdown/markdown_style.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace remin::markdown;

namespace {

int fails = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        ++fails;
        std::cerr << "FAIL: " << msg << "\n";
    }
}

std::filesystem::path temp_dir() {
    char tmpl[] = "/tmp/remin_doc_test_XXXXXX";
    char* p = mkdtemp(tmpl);
    check(p != nullptr, "mkdtemp");
    return std::filesystem::path(p);
}

} // namespace

int main() {
    // --- PrintConfig JSON round trip ---
    {
        PrintConfig cfg;
        cfg.header = "Conf rep";
        cfg.show_page_numbers = false;
        cfg.max_toc_depth = 3;
        cfg.base_font_pt = 10.5;
        const PrintConfig back = PrintConfig::from_json(cfg.to_json());
        check(back.header == cfg.header, "print header round trip");
        check(!back.show_page_numbers, "page numbers off round trip");
        check(back.max_toc_depth == 3, "toc depth round trip");
        check(back.base_font_pt == 10.5, "font size round trip");
        check(PrintConfig::from_json("{broken").show_toc, "malformed json -> defaults");
    }

    // --- save_pasted_image writes assets + returns the right reference ---
    {
        const auto dir = temp_dir();
        MarkdownDocument doc;
        doc.set_note_dir(dir);
        doc.set_asset_dir(dir / "assets");

        const std::string png(64, '\x89');
        const auto ref1 = doc.save_pasted_image(png);
        check(ref1.has_value(), "first paste saved");
        check(*ref1 == "assets/asset-001.png", "first asset name");
        check(std::filesystem::exists(dir / "assets" / "asset-001.png"),
              "asset file written");

        const auto ref2 = doc.save_pasted_image(png);
        check(ref2.has_value() && *ref2 == "assets/asset-002.png", "second asset name");

        check(!doc.save_pasted_image("").has_value(), "empty paste rejected");

        // Re-pasting into a fresh doc (same dir) continues the numbering.
        MarkdownDocument doc2;
        doc2.set_note_dir(dir);
        doc2.set_asset_dir(dir / "assets");
        const auto ref3 = doc2.save_pasted_image(png);
        check(ref3.has_value() && *ref3 == "assets/asset-003.png", "numbering continues");

        std::filesystem::remove_all(dir);
    }

    // --- resolve_asset ---
    {
        const auto dir = temp_dir();
        std::filesystem::create_directories(dir / "assets");
        {
            std::ofstream f(dir / "assets" / "asset-001.png", std::ios::binary);
            f << "PNG";
        }
        std::ofstream f(dir / "pic.svg");
        f << "<svg/>";

        MarkdownDocument doc;
        doc.set_note_dir(dir);
        doc.set_asset_dir(dir / "assets");
        check(doc.resolve_asset("assets/asset-001.png").has_value(), "assets/ rel resolved");
        check(doc.resolve_asset("pic.svg").has_value(), "note-dir relative resolved");
        check(!doc.resolve_asset("assets/missing.png").has_value(), "missing asset rejected");
        check(!doc.resolve_asset("https://x.example/a.png").has_value(), "http rejected");
        check(!doc.resolve_asset("data:image/png;base64,AA==").has_value(), "data uri rejected");
        check(!doc.resolve_asset("").has_value(), "empty ref rejected");

        const auto abs = doc.resolve_asset((dir / "pic.svg").string());
        check(abs.has_value() && abs->filename() == "pic.svg", "absolute resolved");

        std::filesystem::remove_all(dir);
    }

    // --- HTML body: semantic tags carried through ---
    {
        const auto ast = MarkdownAst::parse(
            "# Title\n\nSome **bold** and [link](https://x.example).\n\n"
            "```c\nint x;\n```\n\n"
            "- [x] done\n- [ ] todo\n\n"
            "| a | b |\n|---|--:|\n| 1 | 2 |\n\n"
            "![pic](assets/asset-001.png)\n");
        const std::string html = render_html_body(ast);
        check(html.find("<h1") != std::string::npos, "h1 emitted");
        check(html.find("<strong>") != std::string::npos, "strong emitted");
        check(html.find("<a href=\"https://x.example\"") != std::string::npos, "link emitted");
        check(html.find("<pre>") != std::string::npos, "code block emitted");
        check(html.find("<table>") != std::string::npos, "table emitted");
        check(html.find("<img src=\"assets/asset-001.png\"") != std::string::npos,
              "img src preserved");
        check(html.find("data-checked") != std::string::npos || html.find("checked") != std::string::npos,
              "task item marked");
    }

    // --- HTML body: resolve_asset option rewrites srcs ---
    {
        const auto ast = MarkdownAst::parse("![pic](assets/asset-001.png)");
        HtmlRenderOptions opts;
        opts.resolve_asset = [](const std::string& ref) {
            return std::string("file:///notes/") + ref;
        };
        const std::string html = render_html_body(ast, opts);
        check(html.find("file:///notes/assets/asset-001.png") != std::string::npos,
              "resolved asset src");
    }

    // --- build_html_document wraps body with doctype + title ---
    {
        const std::string html = build_html_document("<h1>x</h1>", "body{}", "My Note");
        check(html.find("<!DOCTYPE html>") != std::string::npos, "doctype");
        check(html.find("<title>My Note</title>") != std::string::npos, "title");
        check(html.find("<h1>x</h1>") != std::string::npos, "body embedded");
        check(html.find("body{}") != std::string::npos, "css embedded");
        check(!builtin_preview_css().empty(), "builtin css non-empty");
    }

    // --- Renderer must NOT report unsupported md4c 0.4.8 features ---
    {
        const auto ast = MarkdownAst::parse("## H\n\nText\n");
        check(!render_html_body(ast, HtmlRenderOptions{}).empty(), "simple doc renders");
    }

    // --- stylesheet engine ---
    {
        const auto style = default_style(light_palette());
        check(style.base_font_pt > 0, "default base font");
        check(style.content_width_pt() > 0, "content width positive");

        const auto merged = apply_style(
            "document { color:#112233; font-size:13pt; line-height:1.8; }\n"
            "code { background:#eeeeee; }\n"
            "bogus-selector { color:red; }",
            style, light_palette());
        check(merged.text_color.valid, "document color set");
        // Dark palette should not poison light defaults.
        const auto dark = default_style(dark_palette());
        check(dark.bg_color.valid, "dark palette background set");
    }

    if (fails == 0) {
        std::cout << "markdown_document_test: ALL PASS\n";
        return 0;
    }
    std::cerr << "markdown_document_test: " << fails << " failure(s)\n";
    return 1;
}