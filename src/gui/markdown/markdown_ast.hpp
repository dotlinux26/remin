#pragma once

#include <string>
#include <vector>

namespace remin::markdown {

// One semantic parse representation for the entire Markdown pipeline
// (preview, HTML export, PDF export). Parsing is done ONCE here (md4c /
// CommonMark) into a small node tree; every renderer walks this tree. No
// renderer is allowed to re-parse Markdown with ad-hoc regexes.

enum class NodeType {
    Root,
    Heading,         // level in `level`
    Paragraph,
    BlockQuote,
    CodeBlock,       // text = verbatim body; info = fence language
    HtmlBlock,       // raw HTML block (emitted raw in HTML; page-break markers -> PageBreak block)
    ThematicBreak,
    List,            // ordered flag; start in `start`; tight in `tight`
    ListItem,        // task/checked in `task`/`checked`
    Table,           // aligns[col] = per-column alignment (derived)
    TableRow,
    TableCell,       // header flag; align = the cell's own alignment (md4c)
    Link,            // text = href; title = title
    Image,           // text = src; title = title; children = alt text
    Emphasis,
    Strong,
    Del,
    CodeSpan,        // text = verbatim body
    Text,            // text = content
    SoftBreak,
    HardBreak,
    HtmlSpan,        // raw inline HTML (emitted raw in HTML)
    Toc,             // [[TOC]] marker — expanded by renderers
};

enum class CellAlign { None, Left, Center, Right };

struct Node {
    NodeType type = NodeType::Text;
    int level = 0;                  // heading level / list nesting (0-based allowed anywhere)
    std::string text;               // text content / src / href / code body / lang markers
    std::string info;               // code fence language
    std::string title;              // link / image title
    bool ordered = false;           // List
    int start = 1;                  // List start index
    bool tight = true;              // List tightness
    bool task = false;              // ListItem is a task item
    bool checked = false;           // ListItem checked
    bool header = false;            // TableCell is a header cell
    CellAlign align = CellAlign::None; // TableCell alignment (from md4c)
    std::vector<CellAlign> aligns;  // Table column alignment
    std::vector<Node> children;
};

class MarkdownAst {
public:
    MarkdownAst() = default;
    explicit MarkdownAst(Node root) : root_(std::move(root)) {}

    // Parse the source with md4c (CommonMark + GFM tables/strikethrough/task
    // lists and permissive autolinks). Pure: no GTK, no CSS.
    static MarkdownAst parse(const std::string& source);

    [[nodiscard]] const Node& root() const { return root_; }
    [[nodiscard]] Node& root() { return root_; }

    struct Heading {
        int level = 0;
        std::string text;   // flattened plain-text heading content
        std::string anchor; // deterministic anchor id
    };
    // All headings in document order with deterministic, deduplicated anchors.
    [[nodiscard]] std::vector<Heading> headings() const;

private:
    Node root_;
};

// Deterministic anchor slug for a heading ("H ello & Wörld!" -> "hello-wörld").
[[nodiscard]] std::string slugify_heading(const std::string& text);

// Decode one raw XML entity (&amp;, &#169;, &#x1f600;). Unknown entities are
// returned unchanged (safe).
[[nodiscard]] std::string decode_entity(const std::string& raw);

} // namespace remin::markdown