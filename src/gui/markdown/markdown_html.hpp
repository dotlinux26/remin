#pragma once

#include "gui/markdown/markdown_ast.hpp"

#include <functional>
#include <string>
#include <vector>

namespace remin::markdown {

// Renders the semantic AST into an HTML fragment. CSS-independent: all visual
// styling is left to the stylesheet layer (the HTML carry only semantic
// classes/attributes). This single renderer feeds both the live preview and
// the HTML export so the two never diverge.
struct HtmlRenderOptions {
    // Max heading depth that a [[TOC]] expands to.
    int toc_max_depth = 6;
    // Called for every image src to decide the emitted URL. Default keeps the
    // source unchanged (used by HTML export, preserving relative assets).
    // The preview passes a resolver mapping relative assets to absolute
    // file:// URLs rooted at the note's asset directory.
    std::function<std::string(const std::string&)> resolve_asset;
};

// Renders the whole document body ([[TOC]] expanded).
[[nodiscard]] std::string render_html_body(const MarkdownAst& ast,
                                           const HtmlRenderOptions& opts = {});

// Renders a table of contents fragment from the document headings.
[[nodiscard]] std::string render_toc_html(const std::vector<MarkdownAst::Heading>& headings,
                                          int max_depth);

// Flatten the inline children of a node into plain text (alt text, etc.).
[[nodiscard]] std::string inline_plain_text(const Node& node);

} // namespace remin::markdown