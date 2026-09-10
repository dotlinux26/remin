#include "gui/markdown/markdown_html.hpp"

#include <map>
#include <sstream>
#include <vector>

namespace remin::markdown {

namespace {

std::string esc_text(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string esc_attr(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

// Anchor deduplication across the whole document ("Intro", "Intro" -> anchors
// "intro" and "intro-2").
class AnchorIndex {
public:
    [[nodiscard]] std::string make(const std::string& section) {
        const std::string base = slugify_heading(section);
        int& used = counts_[base];
        std::string anchor = base;
        if (used > 0) anchor = base + "-" + std::to_string(used + 1);
        ++used;
        return anchor;
    }

private:
    std::map<std::string, int> counts_;
};

struct RenderContext {
    AnchorIndex& anchors;
    const HtmlRenderOptions& opts;
    // Document headings, used to expand [[TOC]].
    const std::vector<MarkdownAst::Heading>& headings;
};

void render_inline(const Node& n, std::string& out, const RenderContext& ctx) {
    switch (n.type) {
        case NodeType::Text:
            out += esc_text(n.text);
            return;
        case NodeType::CodeSpan:
            out += "<code>" + esc_text(n.text) + "</code>";
            return;
        case NodeType::Emphasis:
            out += "<em>";
            for (const Node& c : n.children) render_inline(c, out, ctx);
            out += "</em>";
            return;
        case NodeType::Strong:
            out += "<strong>";
            for (const Node& c : n.children) render_inline(c, out, ctx);
            out += "</strong>";
            return;
        case NodeType::Del:
            out += "<del>";
            for (const Node& c : n.children) render_inline(c, out, ctx);
            out += "</del>";
            return;
        case NodeType::Link:
            out += "<a href=\"" + esc_attr(n.text) + "\"";
            if (!n.title.empty()) out += " title=\"" + esc_attr(n.title) + "\"";
            out += ">";
            for (const Node& c : n.children) render_inline(c, out, ctx);
            out += "</a>";
            return;
        case NodeType::Image: {
            std::string src = ctx.opts.resolve_asset ? ctx.opts.resolve_asset(n.text) : n.text;
            out += "<img src=\"" + esc_attr(src) + "\" alt=\"" +
                   esc_attr(inline_plain_text(n)) + "\"";
            if (!n.title.empty()) out += " title=\"" + esc_attr(n.title) + "\"";
            out += ">";
            return;
        }
        case NodeType::SoftBreak:
            out += ' ';
            return;
        case NodeType::HardBreak:
            out += "<br>\n";
            return;
        case NodeType::HtmlSpan:
        case NodeType::HtmlBlock:
            // Raw HTML is deliberately not emitted (kept safe, matching the
            // old Pango preview that dropped it).
            return;
        default:
            for (const Node& c : n.children) render_inline(c, out, ctx);
            return;
    }
}

std::string cell_align_attr(CellAlign a) {
    switch (a) {
        case CellAlign::Left: return " align=\"left\"";
        case CellAlign::Center: return " align=\"center\"";
        case CellAlign::Right: return " align=\"right\"";
        default: return "";
    }
}

// Node types that are rendered inline (md4c emits raw inline content directly
// under a ListItem when the list is "tight" — no Paragraph wrapper).
bool is_inline_node(NodeType t) {
    switch (t) {
        case NodeType::Text:
        case NodeType::CodeSpan:
        case NodeType::Emphasis:
        case NodeType::Strong:
        case NodeType::Del:
        case NodeType::Link:
        case NodeType::Image:
        case NodeType::SoftBreak:
        case NodeType::HardBreak:
        case NodeType::HtmlSpan:
            return true;
        default:
            return false;
    }
}

void render_block(const Node& n, std::string& out, RenderContext& ctx);

void render_children_as_blocks(const Node& n, std::string& out, RenderContext& ctx) {
    for (const Node& c : n.children) render_block(c, out, ctx);
}

void render_block(const Node& n, std::string& out, RenderContext& ctx) {
    switch (n.type) {
        case NodeType::Heading: {
            const std::string anchor = ctx.anchors.make(inline_plain_text(n));
            out += "<h" + std::to_string(n.level) + " id=\"" + esc_attr(anchor) + "\">";
            for (const Node& c : n.children) render_inline(c, out, ctx);
            out += "</h" + std::to_string(n.level) + ">\n";
            return;
        }
        case NodeType::Paragraph:
            out += "<p>";
            for (const Node& c : n.children) render_inline(c, out, ctx);
            out += "</p>\n";
            return;
        case NodeType::BlockQuote:
            out += "<blockquote>\n";
            render_children_as_blocks(n, out, ctx);
            out += "</blockquote>\n";
            return;
        case NodeType::CodeBlock:
            out += "<pre><code class=\"language-" + esc_attr(n.info) + "\">";
            out += esc_text(n.text);
            out += "</code></pre>\n";
            return;
        case NodeType::HtmlBlock:
            return; // dropped for safety
        case NodeType::ThematicBreak:
            out += "<hr>\n";
            return;
        case NodeType::List: {
            if (n.ordered) out += "<ol start=\"" + std::to_string(n.start) + "\">\n";
            else out += "<ul>\n";
            for (const Node& li : n.children) {
                out += "<li";
                if (li.type == NodeType::ListItem && li.task)
                    out += " class=\"task-list-item\"";
                out += ">";
                if (li.type == NodeType::ListItem && li.task) {
                    out += li.checked ? "<input type=\"checkbox\" checked disabled> "
                                      : "<input type=\"checkbox\" disabled> ";
                }
                // List item children are block nodes; but tight-list items
                // carry their text as inline nodes directly under the item
                // (md4c omits the Paragraph wrapper). Emit the first inline
                // content inline so the item text stays on the checkbox line,
                // and pass genuine blocks through render_block().
                bool first = true;
                for (const Node& inner : li.children) {
                    if (inner.type == NodeType::Paragraph) {
                        if (first) {
                            for (const Node& c : inner.children) render_inline(c, out, ctx);
                        } else {
                            render_block(inner, out, ctx);
                        }
                    } else if (is_inline_node(inner.type)) {
                        render_inline(inner, out, ctx);
                    } else {
                        render_block(inner, out, ctx);
                    }
                    first = false;
                }
                out += "</li>\n";
            }
            if (n.ordered) out += "</ol>\n";
            else out += "</ul>\n";
            return;
        }
        case NodeType::Table: {
            out += "<table>\n";
            if (!n.children.empty()) {
                out += "<thead>\n<tr>\n";
                const auto& row = n.children.front();
                for (std::size_t col = 0; col < row.children.size(); ++col) {
                    const Node& cell = row.children[col];
                    CellAlign a = col < n.aligns.size() ? n.aligns[col] : CellAlign::None;
                    out += "<th" + cell_align_attr(a) + ">";
                    for (const Node& c : cell.children) render_inline(c, out, ctx);
                    out += "</th>\n";
                }
                out += "</tr>\n</thead>\n";
            }
            if (n.children.size() > 1) {
                out += "<tbody>\n";
                for (std::size_t ri = 1; ri < n.children.size(); ++ri) {
                    const auto& row = n.children[ri];
                    out += "<tr>\n";
                    for (std::size_t col = 0; col < row.children.size(); ++col) {
                        const Node& cell = row.children[col];
                        CellAlign a = col < n.aligns.size() ? n.aligns[col] : CellAlign::None;
                        out += "<td" + cell_align_attr(a) + ">";
                        for (const Node& c : cell.children) render_inline(c, out, ctx);
                        out += "</td>\n";
                    }
                    out += "</tr>\n";
                }
                out += "</tbody>\n";
            }
            out += "</table>\n";
            return;
        }
        case NodeType::Toc:
            out += render_toc_html(ctx.headings, ctx.opts.toc_max_depth);
            return;
        default:
            render_children_as_blocks(n, out, ctx);
            return;
    }
}

// ---- Table of contents ----------------------------------------------------

struct TocNode {
    const MarkdownAst::Heading* h;
    int level;
    std::vector<int> children;
};

void emit_toc(const std::vector<TocNode>& nodes, int id, std::vector<int>& counters,
              int depth, std::ostringstream& out) {
    const TocNode& n = nodes[static_cast<std::size_t>(id)];
    if (counters.size() <= static_cast<std::size_t>(depth)) counters.resize(depth + 1, 0);
    ++counters[depth];
    std::string num;
    for (int d = 0; d <= depth; ++d) {
        if (d) num += ".";
        num += std::to_string(counters[d]);
    }
    out << "<li><a href=\"#" << esc_attr(n.h->anchor) << "\">"
        << esc_text(num + ". " + n.h->text) << "</a>";
    if (!n.children.empty()) {
        out << "<ol>";
        for (int c : n.children) emit_toc(nodes, c, counters, depth + 1, out);
        out << "</ol>";
    }
    out << "</li>";
}

} // namespace

std::string inline_plain_text(const Node& node) {
    switch (node.type) {
        case NodeType::Text:
        case NodeType::CodeSpan:
            return node.text;
        case NodeType::SoftBreak:
        case NodeType::HardBreak:
            return " ";
        case NodeType::Image:
            return node.title.empty() ? node.text : node.title;
        default:
            break;
    }
    std::string out;
    for (const Node& c : node.children) out += inline_plain_text(c);
    return out;
}

std::string render_toc_html(const std::vector<MarkdownAst::Heading>& headings,
                            int max_depth) {
    std::ostringstream out;
    out << "<nav class=\"toc\"><p class=\"toc-title\">Contents</p><ol>\n";
    if (!headings.empty()) {
        // Include only headings up to the configured depth.
        std::vector<MarkdownAst::Heading> filtered;
        for (const auto& h : headings)
            if (h.level <= max_depth) filtered.push_back(h);

        std::vector<TocNode> nodes;
        nodes.reserve(filtered.size());
        for (const auto& h : filtered)
            nodes.push_back(TocNode{&h, h.level, {}});

        // Parent of each entry = nearest earlier entry whose level is lower.
        std::vector<int> roots;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            int parent = -1;
            for (std::size_t j = i; j-- > 0;) {
                if (nodes[j].level < nodes[i].level) { parent = static_cast<int>(j); break; }
            }
            if (parent >= 0) nodes[static_cast<std::size_t>(parent)].children.push_back(
                                 static_cast<int>(i));
            else roots.push_back(static_cast<int>(i));
        }

        std::vector<int> counters;
        for (int r : roots) emit_toc(nodes, r, counters, 0, out);
    }
    out << "</ol></nav>\n";
    return out.str();
}

std::string render_html_body(const MarkdownAst& ast, const HtmlRenderOptions& opts) {
    const std::vector<MarkdownAst::Heading> headings = ast.headings();
    AnchorIndex anchors;
    RenderContext ctx{anchors, opts, headings};
    std::string out;
    for (const Node& c : ast.root().children) render_block(c, out, ctx);
    return out;
}

} // namespace remin::markdown