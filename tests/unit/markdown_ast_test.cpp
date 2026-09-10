// Unit tests for the Remin Markdown AST (md4c) and serialization helpers.
//
// Pure subject: md4c parse + heading anchor generation + entity decoding.
#include "gui/markdown/markdown_ast.hpp"

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

} // namespace

int main() {
    // --- parse produces a Root with content ---
    {
        auto ast = MarkdownAst::parse("# Hello\n\nworld");
        check(ast.root().type == NodeType::Root, "root type");
        check(ast.root().children.size() == 2, "two top-level blocks");
        check(ast.root().children[0].type == NodeType::Heading, "heading block");
        check(ast.root().children[0].level == 1, "heading level 1");
        check(ast.root().children[1].type == NodeType::Paragraph, "paragraph block");
    }

    // --- inline emphasis / strong / code span / link flatten ---
    {
        auto ast = MarkdownAst::parse("**bold** and *em* and `code`");
        check(ast.root().children.size() == 1, "one paragraph");
        const Node& p = ast.root().children[0];
        bool saw_strong = false, saw_em = false, saw_code = false, saw_text = false;
        for (const auto& c : p.children) {
            saw_strong = saw_strong || c.type == NodeType::Strong;
            saw_em = saw_em || c.type == NodeType::Emphasis;
            saw_code = saw_code || c.type == NodeType::CodeSpan;
            saw_text = saw_text || c.type == NodeType::Text;
        }
        check(saw_strong, "strong parsed");
        check(saw_em, "emphasis parsed");
        check(saw_code, "code span parsed");
        check(saw_text, "plain text parsed");
    }

    // --- [link](href) + image [alt](src) ---
    {
        auto ast = MarkdownAst::parse("[click](https://x.example) ![logo](a.png)");
        const Node& p = ast.root().children[0];
        bool saw_link = false, saw_image = false;
        for (const auto& c : p.children) {
            if (c.type == NodeType::Link) {
                saw_link = true;
                check(c.text == "https://x.example", "link href captured");
                check(c.children.size() == 1 && c.children[0].type == NodeType::Text,
                      "link text flattened");
            }
            if (c.type == NodeType::Image) {
                saw_image = true;
                check(c.text == "a.png", "image src captured");
            }
        }
        check(saw_link, "link parsed");
        check(saw_image, "image parsed");
    }

    // --- GFM: table ---
    {
        auto ast = MarkdownAst::parse("| a | b |\n|---|--:|\n| 1 | 2 |");
        check(!ast.root().children.empty(), "table doc not empty");
        const Node& first = ast.root().children[0];
        check(first.type == NodeType::Table, "table block parsed");
    }

    // --- GFM: table with short delimiter cells ("| - |") ---
    // System md4c 0.4.8 rejects delimiter cells shorter than 3 dashes and the
    // whole table degrades to a paragraph; the vendored 0.5.3 must parse it.
    {
        auto ast = MarkdownAst::parse("| # | Project | Contract |\n| - | ------- | ------: |\n| 1 | libfoo | 100 |");
        check(!ast.root().children.empty(), "short-delimiter table doc not empty");
        const Node& first = ast.root().children[0];
        check(first.type == NodeType::Table, "short-delimiter table parsed");
        check(first.children.size() == 2, "header + one data row");
        const Node& header = first.children[0];
        check(header.type == NodeType::TableRow, "header row node");
        check(header.children.size() == 3, "three columns");
        check(header.children[2].align == CellAlign::Right, "right align from ':'");
    }

    // --- GFM: task list + strikethrough ---
    {
        auto ast = MarkdownAst::parse("- [x] done\n- [ ] todo\n\n~~gone~~");
        bool saw_task = false;
        bool saw_check = false;
        bool saw_del = false;
        const auto& root = ast.root().children;
        for (const auto& block : root) {
            if (block.type == NodeType::List) {
                for (const auto& li : block.children) {
                    if (li.task) saw_task = true;
                    if (li.checked) saw_check = true;
                }
            }
            if (block.type == NodeType::Paragraph) {
                for (const auto& c : block.children)
                    if (c.type == NodeType::Del) saw_del = true;
            }
        }
        check(saw_task, "task list item parsed");
        check(saw_check, "checked item parsed");
        check(saw_del, "strikethrough parsed");
    }

    // --- [[TOC]] marker ---
    {
        auto ast = MarkdownAst::parse("[[TOC]]\n\n# Intro");
        check(ast.root().children[0].type == NodeType::Toc,
              "TOC paragraph becomes Toc node");
    }

    // --- heading anchors: deterministic + deduplicated ---
    {
        auto ast = MarkdownAst::parse("# Hello World\n# Hello World\n## Hello, world!");
        const auto hs = ast.headings();
        check(hs.size() == 3, "three headings collected");
        check(hs[0].anchor == "hello-world", "anchor 1 slug");
        check(hs[1].anchor == "hello-world-2", "duplicate heading deduplicated");
        check(hs[2].anchor == "hello-world-3", "third collision deduplicated");
        check(hs[2].level == 2, "heading level captured");
        check(hs[0].text == "Hello World", "heading plain text");
    }

    // --- entity decoding ---
    {
        check(decode_entity("&amp;") == "&", "amp entity");
        check(decode_entity("&#x41;") == "A", "hex entity");
        check(decode_entity("hello") == "hello", "plain unchanged");
        check(decode_entity("&bogus;") == "&bogus;", "unknown entity unchanged");
    }

    // --- slugify ---
    {
        check(slugify_heading("H ello & World!") == "h-ello-world", "slug lower + hyphen");
        check(slugify_heading("+++") == "section", "empty slug falls back");
    }

    if (fails == 0) {
        std::cout << "markdown_ast_test: ALL PASS\n";
        return 0;
    }
    std::cerr << "markdown_ast_test: " << fails << " failure(s)\n";
    return 1;
}