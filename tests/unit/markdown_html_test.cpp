// Unit tests for the Remin Markdown HTML renderer.
//
// Pure subject: markdown_ast -> render_html_body (tight-list inline content,
// short-dash tables, heading anchors, [[TOC]] expansion).
#include "gui/markdown/markdown_ast.hpp"
#include "gui/markdown/markdown_html.hpp"

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

void check_contains(const std::string& html, const std::string& needle,
                    const char* msg) {
    if (html.find(needle) == std::string::npos) {
        ++fails;
        std::cerr << "FAIL: " << msg << " (missing: " << needle << ")\n";
    }
}

} // namespace

int main() {
    // --- tight list: inline content directly under the item ---
    {
        auto ast = MarkdownAst::parse("- alpha\n- **bold beta**\n");
        std::string html = render_html_body(ast);
        check_contains(html, "<li>alpha</li>", "tight item text emitted");
        check_contains(html, "<li><strong>bold beta</strong></li>",
                       "tight item inline formatting emitted");
    }

    // --- loose list keeps the Paragraph structure ---
    {
        auto ast = MarkdownAst::parse("- one\n\n- two\n");
        std::string html = render_html_body(ast);
        check_contains(html, "<li>one</li>", "loose item text inlined");
        check_contains(html, "<li>two</li>", "second loose item text inlined");
    }

    // --- nested list inside a tight item ---
    {
        auto ast = MarkdownAst::parse("- top\n  - sub\n");
        std::string html = render_html_body(ast);
        check_contains(html, "<li>top", "nested-list parent opened");
        check_contains(html, "<li>sub</li>", "nested-list child emitted");
    }

    // --- table with short delimiter cells renders as <table> ---
    {
        auto ast = MarkdownAst::parse("| # | Name |\n| - | ---: |\n| 1 | Foo |");
        std::string html = render_html_body(ast);
        check_contains(html, "<table>", "short-dash table opened");
        check_contains(html, "<th>#</th>", "table header cell emitted");
        check_contains(html, "<td align=\"right\">Foo</td>", "table data cell emitted");
    }

    // --- heading anchor ids are emitted ---
    {
        auto ast = MarkdownAst::parse("# Hello World");
        std::string html = render_html_body(ast);
        check_contains(html, "<h1 id=\"hello-world\">Hello World</h1>",
                       "heading anchor emitted");
    }

    // --- [[TOC]] expands to a nav with links ---
    {
        auto ast = MarkdownAst::parse("[[TOC]]\n\n# Intro\n## Deep");
        std::string html = render_html_body(ast);
        check_contains(html, "<nav class=\"toc\">", "toc nav emitted");
        check_contains(html, "href=\"#intro\"", "toc link to intro");
    }

    if (fails == 0) {
        std::cout << "markdown_html_test: ALL PASS\n";
        return 0;
    }
    std::cerr << "markdown_html_test: " << fails << " failure(s)\n";
    return 1;
}