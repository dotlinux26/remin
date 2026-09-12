#include "gui/markdown/markdown_layout.hpp"

#include "gui/markdown/markdown_html.hpp"  // inline_plain_text (anchors)
#include "gui/markdown/markdown_inline_html.hpp"  // inline HTML/CSS subset
#include "gui/markdown/markdown_pango.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <cmath>
#include <cctype>

namespace remin::markdown {

bool LayoutResult::block_keeps_with_next(const Block& b) const {
    return b.kind == Block::Kind::Heading;
}

namespace {

std::string trim_ws(const std::string& s) {
    auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    };
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && is_space(s[a])) ++a;
    while (b > a && is_space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Collect inline styled runs from an AST node into `out`.
class RunBuilder {
public:
    RunBuilder(const StyleSheet& s, std::vector<StyledText>& out)
        : style_(s), out_(out) {
        reset(s.base_font, s.base_font_pt, s.text_color);
    }

    void reset(const std::string& font, double size, Color color) {
        font_ = font;
        size_pt_ = size;
        weight_ = 400;
        italic_ = strike_ = underline_ = false;
        color_ = color;
        bg_.valid = false;
        is_link_ = false;
        href_.clear();
        anchor_.clear();
        html_stack_.clear();
    }

    void set_anchor(const std::string& a) { anchor_ = a; }
    [[nodiscard]] const std::string& anchor() const { return anchor_; }
    void set_weight(int w) { weight_ = w; }

    // Apply an inline HTML/CSS style override onto the current run state. The
    // fields not named by the override stay untouched (inherit) so the subset
    // may only affect what it explicitly declares.
    void apply(const InlineCssStyle& css) {
        if (css.has_color) color_ = css.color;
        if (css.has_bg) bg_ = css.background;
        if (css.weight > 0) weight_ = css.weight;
        if (css.italic_set) italic_ = css.italic;
        if (css.underline_set) underline_ = css.underline;
        if (css.strike_set) strike_ = css.strike;
        if (css.size_pt > 0.0) size_pt_ = css.size_pt;
        else if (css.size_pct > 0.0) size_pt_ *= css.size_pct;
        if (!css.font_family.empty()) font_ = css.font_family;
    }

    void push(const std::string& t) {
        if (t.empty()) return;
        out_.push_back(StyledText{t, font_, size_pt_, weight_, italic_, strike_,
                                  underline_, color_, bg_, is_link_, href_, anchor_});
    }

    void emit(const Node& n);

private:
    struct RunState {
        std::string font;
        double size_pt;
        int weight;
        bool italic, strike, underline;
        Color color, bg;
    };

    [[nodiscard]] RunState snapshot() const {
        return RunState{font_, size_pt_, weight_, italic_, strike_, underline_,
                        color_, bg_};
    }

    void restore(RunState s) {
        font_ = s.font;
        size_pt_ = s.size_pt;
        weight_ = s.weight;
        italic_ = s.italic;
        strike_ = s.strike;
        underline_ = s.underline;
        color_ = s.color;
        bg_ = s.bg;
    }

    const StyleSheet& style_;
    std::vector<StyledText>& out_;
    std::string font_;
    double size_pt_;
    int weight_;
    bool italic_, strike_, underline_;
    Color color_, bg_;
    bool is_link_;
    std::string href_;
    std::string anchor_;
    std::vector<RunState> html_stack_;
};

void RunBuilder::emit(const Node& n) {
    switch (n.type) {
        case NodeType::Text:
            push(n.text);
            return;
        case NodeType::CodeSpan: {
            const TextStyle code = style_.text[static_cast<int>(StyleSelector::Code)];
            const std::string f = font_;
            const double s = size_pt_;
            const Color c = color_, b = bg_;
            font_ = "monospace";
            size_pt_ = code.font_size_pt > 0.0 ? code.font_size_pt : s;
            color_ = code.color.valid ? code.color : c;
            bg_ = code.background.valid ? code.background : b;
            underline_ = false;
            strike_ = false;
            // md4c stores the code body in `text` with no child Text nodes.
            if (n.text.empty()) {
                for (const Node& ch : n.children) emit(ch);
            } else {
                push(n.text);
            }
            font_ = f;
            size_pt_ = s;
            color_ = c;
            bg_ = b;
            return;
        }
        case NodeType::Emphasis: {
            bool save = italic_;
            italic_ = true;
            for (const Node& c : n.children) emit(c);
            italic_ = save;
            return;
        }
        case NodeType::Strong: {
            int save = weight_;
            weight_ = 700;
            for (const Node& c : n.children) emit(c);
            weight_ = save;
            return;
        }
        case NodeType::Del: {
            bool save = strike_;
            strike_ = true;
            for (const Node& c : n.children) emit(c);
            strike_ = save;
            return;
        }
        case NodeType::Link: {
            const bool save_ul = underline_;
            const Color save_col = color_;
            underline_ = true;
            color_ = style_.link_color;
            const std::string save_href = href_;
            href_ = n.text;
            is_link_ = true;
            for (const Node& c : n.children) emit(c);
            underline_ = save_ul;
            color_ = save_col;
            href_ = save_href;
            is_link_ = false;
            return;
        }
        case NodeType::Image:
            // Inline images between text are simplified out (block-level
            // images are handled by the caller). Alt text is kept.
            for (const Node& c : n.children) emit(c);
            return;
        case NodeType::SoftBreak:
            push(" ");
            return;
        case NodeType::HardBreak:
            push("\n");
            return;
        case NodeType::HtmlSpan: {
            const auto tag = parse_html_tag(n.text);
            if (!tag) return;
            if (tag->closing) {
                if (!html_stack_.empty()) {
                    restore(html_stack_.back());
                    html_stack_.pop_back();
                }
                return;
            }
            html_stack_.push_back(snapshot());
            apply(style_from_tag(tag->name, tag->style, style_));
            for (const Node& c : n.children) emit(c);
            return;
        }
        case NodeType::HtmlBlock:
            // Block-level HTML is handled in emit_node (wrapper context or
            // whole-chunk renderer); nothing inline lives under it.
            for (const Node& c : n.children) emit(c);
            return;
        default:
            for (const Node& c : n.children) emit(c);
            return;
    }
}

// Heading anchors: same slugify pipeline as the AST module so link targets
// line up between preview, HTML and PDF.
std::string heading_anchor(const Node& n, const MarkdownAst& ast) {
    // The AST already assigns anchors in document order (duplicates deduped).
    const auto headings = ast.headings();
    const std::string flat = inline_plain_text(n);
    for (const auto& h : headings)
        if (h.text == flat) return h.anchor;
    return slugify_heading(flat);
}

// Rough inline width (pt) of a marker string using base font metrics.
double marker_width(const StyleSheet& s, const std::string& marker) {
    StyledText run;
    run.text = marker;
    run.font_family = s.base_font;
    run.size_pt = s.base_font_pt;
    run.color = s.text_color;
    const std::vector<StyledText> runs{run};
    return measure_runs(runs, -1.0, s).width;
}

// Resolve and measure a block-level image.
bool resolve_image_block(const Node& n, const ImageResolver& resolve_image,
                         double content_width,
                         Block& out) {
    const std::string src = n.text;
    if (src.empty()) return false;
    auto path = resolve_image ? resolve_image(src) : std::optional<std::filesystem::path>{};
    if (!path && src.find("://") == std::string::npos && src.rfind("data:", 0) != 0)
        return false;  // unresolvable local ref — leave a gap paragraph?
    if (!path) return false;

    GError* err = nullptr;
    GdkPixbuf* pb = gdk_pixbuf_new_from_file(path->c_str(), &err);
    if (!pb) {
        if (err) g_error_free(err);
        return false;
    }
    const int pw = gdk_pixbuf_get_width(pb);
    const int ph = gdk_pixbuf_get_height(pb);
    g_object_unref(pb);
    if (pw <= 0 || ph <= 0) return false;

    try {
        const double w = static_cast<double>(pw);
        const double h = static_cast<double>(ph);
        double scale = content_width / w;
        if (scale >= 1.0) scale = 1.0;  // never upscale
        out.has_image = true;
        out.image_path = *path;
        out.image_w = w * scale;
        out.image_h = h * scale;
        out.height = out.image_h;
        out.kind = Block::Kind::Image;
        return true;
    } catch (...) {
        return false;
    }
}

// ---- table helpers --------------------------------------------------------

struct CellRuns {
    std::vector<StyledText> runs;
    CellAlign align = CellAlign::None;
    bool header = false;
};

void build_cell_run(const Node& cell, const StyleSheet& style, std::vector<StyledText>& out) {
    RunBuilder rb(style, out);
    if (cell.header) {
        const StyleSelector s = StyleSelector::TableHeader;
        rb.reset(style.font_for(s), style.font_size_for(s), style.color_for(s));
        rb.set_weight(std::max(style.text[static_cast<int>(s)].weight, 700));
    } else {
        rb.reset(style.base_font, style.font_size_for(StyleSelector::Table),
                 style.color_for(StyleSelector::Table));
    }
    for (const Node& c : cell.children) rb.emit(c);
}

// Width of the widest *word* (unbreakable token) across a cell's runs, i.e. the
// column's minimum width: Pango only breaks on spaces, so a column narrower
// than this would split words mid-way (the "Severit/ty" effect). We force each
// token onto its own line by turning spaces into newlines and measuring — the
// widest resulting line is exactly the widest token.
double widest_word_width(const std::vector<StyledText>& runs, const StyleSheet& style) {
    std::vector<StyledText> copy = runs;
    for (StyledText& r : copy) {
        for (char& ch : r.text)
            if (ch == ' ' || ch == '\t') ch = '\n';
    }
    return measure_runs(copy, -1.0, style).width;
}

} // namespace

bool is_page_break_marker(const std::string& html_block_source) {
    auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    std::string s = html_block_source;
    while (!s.empty() && is_space(s.front())) s.erase(s.begin());
    while (!s.empty() && is_space(s.back())) s.pop_back();
    if (s.empty()) return false;
    if (s.front() != '<') return false;

    // Only block-level elements that carry an inline style attribute.
    const std::string lower = [&] {
        std::string out;
        out.reserve(s.size());
        for (char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }();

    const bool known_tag =
        lower.rfind("<div", 0) == 0 || lower.rfind("<section", 0) == 0 ||
        lower.rfind("<p", 0) == 0 || lower.rfind("<span", 0) == 0;
    if (!known_tag) return false;

    const bool break_before =
        lower.find("page-break-before:") != std::string::npos ||
        lower.find("break-before:") != std::string::npos;
    const bool break_after =
        lower.find("page-break-after:") != std::string::npos ||
        lower.find("break-after:") != std::string::npos;
    if (!break_before && !break_after) return false;

    const bool wants_pretty =
        lower.find(": always") != std::string::npos ||
        lower.find(": page") != std::string::npos;
    return wants_pretty;
}

LayoutResult layout_document(const MarkdownAst& ast, const StyleSheet& style,
                              double content_width_pt,
                              const ImageResolver& resolve_image,
                              const TocPageResolver& toc_pages,
                              bool render_toc) {
    LayoutResult res;
    std::vector<Block>& blocks = res.blocks;

    const auto push_block = [&](Block b) {
        blocks.push_back(std::move(b));
    };

    // Open block-level HTML wrappers (Case A: <div ...> + Paragraph + </div>).
    // The wrapper style applies onto every following text block until a matching
    // closing HtmlBlock pops it.
    std::vector<InlineCssStyle> block_ctx;
    const auto apply_block_ctx = [&](RunBuilder& rb) {
        for (const InlineCssStyle& cs : block_ctx) rb.apply(cs);
    };

    // ---- flow assembly -----------------------------------------------------
    const Node& root = ast.root();
    double y = 0.0;

    const auto append_paragraph = [&](const Node& p) {
        Block b;
        b.kind = Block::Kind::P;
        b.margin_before = style.box_for(StyleSelector::Paragraph).margin_top_pt;
        b.margin_after = style.box_for(StyleSelector::Paragraph).margin_bottom_pt;
        std::vector<StyledText> runs;
        RunBuilder rb(style, runs);
        rb.reset(style.base_font, style.base_font_pt, style.text_color);
        apply_block_ctx(rb);
        for (const Node& c : p.children) rb.emit(c);
        b.run = std::move(runs);
        if (b.run.empty()) return;  // empty paragraph -> no box
        const auto m = measure_runs(b.run, content_width_pt, style);
        b.content_width = content_width_pt;
        b.height = m.height;
        b.baseline = m.baseline_pt;
        // Paragraph hyperlink: only when the whole paragraph is one uniform
        // link (single distinct href). Mixed-text paragraphs cannot be
        // expressed by a single block-level `<link>` tag, so they stay plain.
        {
            std::string href;
            bool uniform = true;
            for (const StyledText& r : b.run) {
                if (r.is_link && !r.href.empty()) {
                    if (href.empty()) {
                        href = r.href;
                    } else if (href != r.href) {
                        uniform = false;
                        break;
                    }
                }
            }
            if (uniform && !href.empty()) {
                b.has_link_hit = true;
                b.link_href = href;
                b.link_x = 0;
                b.link_w = b.content_width;
                b.link_h = b.height;
            }
        }
        b.y = y + b.margin_before;
        y = b.y + b.height + b.margin_after;
        push_block(std::move(b));
    };

    const auto append_heading = [&](const Node& h, int level) {
        const StyleSelector sel = static_cast<StyleSelector>(
            static_cast<int>(StyleSelector::H1) + std::clamp(level - 1, 0, 5));
        Block b;
        b.kind = Block::Kind::Heading;
        b.level = level;
        b.margin_before = style.box_for(sel).margin_top_pt;
        b.margin_after = style.box_for(sel).margin_bottom_pt;
        std::vector<StyledText> runs;
        RunBuilder rb(style, runs);
        rb.reset(style.font_for(sel), style.font_size_for(sel), style.color_for(sel));
        apply_block_ctx(rb);
        rb.set_weight(style.text[static_cast<int>(sel)].weight > 0
                          ? style.text[static_cast<int>(sel)].weight : 400);
        rb.set_anchor(heading_anchor(h, ast));
        for (const Node& c : h.children) rb.emit(c);
        b.run = std::move(runs);
        if (b.run.empty()) return;
        const auto m = measure_runs(b.run, content_width_pt, style);
        b.content_width = content_width_pt;
        b.height = m.height;
        b.baseline = m.baseline_pt;
        // heading hyperlink hit rect (the whole heading line box)
        b.has_link_hit = true;
        b.link_x = 0;
        b.link_w = m.width;
        b.link_h = m.height;
        b.link_href = "#" + rb.anchor();
        b.y = y + b.margin_before;
        y = b.y + b.height + b.margin_after;
        push_block(std::move(b));
    };

    // ---- recursive block emitter ------------------------------------------
    std::function<void(const Node&)> emit_node;
    emit_node = [&](const Node& n) {
        switch (n.type) {
            case NodeType::Heading:
                append_heading(n, std::max(1, n.level));
                return;
            case NodeType::Paragraph: {
                // A paragraph containing only an image -> block image.
                if (n.children.size() == 1 &&
                    n.children[0].type == NodeType::Image) {
                    Block b;
                    b.margin_before = 6.0;
                    b.margin_after = 6.0;
                    if (resolve_image_block(n.children[0], resolve_image,
                                            content_width_pt, b)) {
                        b.y = y + b.margin_before;
                        y = b.y + b.height + b.margin_after;
                        push_block(std::move(b));
                    }
                    return;
                }
                append_paragraph(n);
                return;
            }
            case NodeType::BlockQuote: {
                // Flatten quote children into one block with quote visuals.
                std::vector<StyledText> runs;
                bool first_line = true;
                const auto walk = [&](const auto& self, const Node& q) -> void {
                    for (const Node& c : q.children) {
                        if (c.type == NodeType::Paragraph) {
                            if (!first_line) {
                                StyledText gap;
                                gap.text = "\n";
                                gap.font_family = style.base_font;
                                gap.size_pt = style.base_font_pt;
                                gap.color = style.text_color;
                                runs.push_back(gap);
                            }
                            first_line = false;
                            RunBuilder rb(style, runs);
                            rb.reset(style.base_font, style.base_font_pt, style.text_color);
                            apply_block_ctx(rb);
                            for (const Node& ic : c.children) rb.emit(ic);
                        } else if (c.type == NodeType::BlockQuote) {
                            self(self, c);
                        }
                    }
                };
                walk(walk, n);
                if (runs.empty()) return;
                Block b;
                b.kind = Block::Kind::Quote;
                const auto qbox = style.box_for(StyleSelector::Blockquote);
                const Color qbg = style.bg_color_for(StyleSelector::Blockquote);
                b.bg = qbg;
                b.border = style.box_for(StyleSelector::Blockquote).border_color;
                b.has_bg = qbg.valid;
                b.border_left_w = style.box_for(StyleSelector::Blockquote).border_left_width_pt;
                b.padding = qbox.padding_pt;
                b.margin_before = qbox.margin_top_pt;
                b.margin_after = qbox.margin_bottom_pt;
                const double inner = content_width_pt - 2.0 * b.padding;
                b.run = std::move(runs);
                const auto m = measure_runs(b.run, inner, style);
                b.content_width = inner;
                b.height = m.height + 2.0 * b.padding;
                b.baseline = m.baseline_pt + b.padding;
                b.y = y + b.margin_before;
                y = b.y + b.height + b.margin_after;
                (void)b.border;
                push_block(std::move(b));
                return;
            }
            case NodeType::CodeBlock: {
                Block b;
                b.kind = Block::Kind::Code;
                b.is_code = true;
                b.code_lang = n.info;
                const auto pbox = style.box_for(StyleSelector::Pre);
                b.bg = style.bg_color_for(StyleSelector::Pre);
                b.has_bg = b.bg.valid;
                b.outer_border = style.box_for(StyleSelector::Pre).border_color;
                b.border_w = style.box_for(StyleSelector::Pre).border_width_pt;
                b.padding = pbox.padding_pt;
                b.margin_before = pbox.margin_top_pt;
                b.margin_after = pbox.margin_bottom_pt;
                // Reserve room at the top for the language badge so the first
                // code line never collides with it.
                b.badge_room = 0.0;
                if (!b.code_lang.empty()) {
                    const double badge_bottom = kCodeBadgePad + kCodeBadgeLinePt;
                    b.badge_room = std::max(0.0,
                        badge_bottom + kCodeBadgeGap - pbox.padding_pt);
                }
                const double inner = content_width_pt - 2.0 * b.padding;
                StyledText code_run;
                code_run.text = n.text;
                const TextStyle pre = style.text[static_cast<int>(StyleSelector::Pre)];
                code_run.font_family = "monospace";
                code_run.size_pt = style.font_size_for(StyleSelector::Pre);
                code_run.color = style.color_for(StyleSelector::Pre);
                code_run.background = Color{};  // block bg drawn separately
                b.run = {code_run};
                const auto m = measure_runs(b.run, inner, style);
                b.content_width = inner;
                b.height = m.height + pbox.padding_pt + pbox.padding_pt + b.badge_room;
                b.baseline = m.baseline_pt + pbox.padding_pt + b.badge_room;
                b.y = y + b.margin_before;
                y = b.y + b.height + b.margin_after;
                push_block(std::move(b));
                return;
            }
            case NodeType::ThematicBreak: {
                Block b;
                b.kind = Block::Kind::Hr;
                b.margin_before = 10.0;
                b.margin_after = 10.0;
                b.height = 1.5;
                b.color = style.hr_color;  // NOLINT (field reused for stroke color)
                b.y = y + b.margin_before;
                y = b.y + b.height + b.margin_after;
                push_block(std::move(b));
                return;
            }
            case NodeType::List: {
                int ordinal = n.start;
                for (const Node& li : n.children) {
                    if (li.type != NodeType::ListItem) continue;
                    Block b;
                    b.level = 1;
                    const bool task = li.task;
                    if (task) {
                        b.kind = Block::Kind::TaskItem;
                        b.marker = li.checked ? "\u2611 " : "\u2610 ";  // ☑ / ☐
                    } else if (n.ordered) {
                        b.kind = Block::Kind::OlItem;
                        b.marker = std::to_string(ordinal++) + ". ";
                    } else {
                        b.kind = Block::Kind::UlItem;
                        b.marker = "\u2022 ";  // •
                    }
                    const auto libox = style.box_for(StyleSelector::ListItem);
                    b.margin_before = libox.margin_top_pt;
                    b.margin_after = libox.margin_bottom_pt;
                    // First paragraph of the item becomes the run.
                    std::vector<StyledText> runs;
                    bool have_text = false;
                    for (const Node& inner : li.children) {
                        if (inner.type == NodeType::Paragraph) {
                            RunBuilder rb(style, runs);
                            rb.reset(style.base_font, style.base_font_pt, style.text_color);
                            apply_block_ctx(rb);
                            for (const Node& ic : inner.children) rb.emit(ic);
                            have_text = true;
                        } else if (inner.type == NodeType::List) {
                            // nested list as inline continuation (simple)
                            for (const Node& sub : inner.children) {
                                for (const Node& subinner : sub.children) {
                                    if (subinner.type == NodeType::Paragraph) {
                                        StyledText nl;
                                        nl.text = "\n";
                                        nl.font_family = style.base_font;
                                        nl.size_pt = style.base_font_pt;
                                        nl.color = style.text_color;
                                        runs.push_back(nl);
                                        RunBuilder rb(style, runs);
                                        rb.reset(style.base_font, style.base_font_pt,
                                                 style.text_color);
                                        apply_block_ctx(rb);
                                        for (const Node& ic : subinner.children) rb.emit(ic);
                                    }
                                }
                            }
                        } else {
                            // Tight-list items may expose inline nodes directly
                            // (md4c flattens the Paragraph in tight lists).
                            RunBuilder rb(style, runs);
                            rb.reset(style.base_font, style.base_font_pt, style.text_color);
                            apply_block_ctx(rb);
                            rb.emit(inner);
                            have_text = true;
                        }
                    }
                    if (runs.empty() && !have_text) continue;
                    const double mw = marker_width(style, b.marker);
                    const double text_w = content_width_pt - mw;
                    const double m = text_w > 0 ? text_w : content_width_pt;
                    b.run = std::move(runs);
                    const auto mt = measure_runs(b.run, m > 0 ? m : content_width_pt, style);
                    b.content_width = m > 0 ? m : content_width_pt;
                    b.height = std::max(mt.height, 12.0);
                    b.baseline = mt.baseline_pt;
                    b.y = y + b.margin_before;
                    y = b.y + b.height + b.margin_after;
                    push_block(std::move(b));
                }
                return;
            }
            case NodeType::Table: {
                Block b;
                b.kind = Block::Kind::Table;
                b.is_table = true;
                const auto tbox = style.box_for(StyleSelector::Table);
                b.margin_before = tbox.margin_top_pt;
                b.margin_after = tbox.margin_bottom_pt;
                b.outer_border = style.box_for(StyleSelector::Table).border_color;
                // The table draws its own complete 1.0pt box in
                // draw_blocks_range (left/right edges + separators, on top of
                // the header fill), so the generic block outline is disabled
                // for tables. Drawing the outer box only via the generic path
                // left the left edge hidden under the header fill and thinner
                // than the double-stroked right edge.
                b.border_w = 0.0;
                b.bg = style.bg_color_for(StyleSelector::TableHeader);

                // Build cell runs once.
                struct CellInput {
                    std::vector<StyledText> runs;
                    CellAlign align = CellAlign::None;
                    bool header = false;
                };
                std::vector<std::vector<CellInput>> cells;  // [row][col]
                std::size_t col_count = 0;
                for (const Node& row : n.children) {
                    if (row.type != NodeType::TableRow) continue;
                    std::vector<CellInput> rowcells;
                    for (const Node& cell : row.children) {
                        if (cell.type != NodeType::TableCell) continue;
                        CellInput ci;
                        ci.header = cell.header;
                        ci.align = cell.align;
                        build_cell_run(cell, style, ci.runs);
                        rowcells.push_back(std::move(ci));
                    }
                    col_count = std::max(col_count, rowcells.size());
                    cells.push_back(std::move(rowcells));
                }
                if (cells.empty()) return;
                if (col_count == 0) return;

                // ---- column widths (RFC 1942 / CSS table auto-layout) ----
                // Pass 1: min = widest unbreakable word, max = widest line.
                const double cpad = 5.0;
                b.cell_pad = cpad;
                std::vector<double> min_w(col_count, 0.0), max_w(col_count, 0.0);
                for (auto& row : cells) {
                    for (std::size_t c = 0; c < row.size() && c < col_count; ++c) {
                        const auto m = measure_runs(row[c].runs, -1.0, style);
                        max_w[c] = std::max(max_w[c], m.width);
                        min_w[c] = std::max(min_w[c], widest_word_width(row[c].runs, style));
                    }
                }
                for (std::size_t c = 0; c < col_count; ++c) {
                    max_w[c] += 2.0 * cpad;
                    min_w[c] += 2.0 * cpad;
                }
                double sum_max = 0.0, sum_min = 0.0;
                for (std::size_t c = 0; c < col_count; ++c) {
                    sum_max += max_w[c];
                    sum_min += min_w[c];
                }
                std::vector<double> col_w(col_count, 0.0);
                if (sum_max <= content_width_pt) {
                    col_w = max_w;  // whole table fits at natural size
                } else if (sum_min >= content_width_pt) {
                    col_w = min_w;  // cannot shrink without splitting words
                } else {
                    // Pass 2: stretch each column from its min by a share of the
                    // remaining space proportional to its elasticity (max-min).
                    const double space = content_width_pt - sum_min;
                    const double elastic = sum_max - sum_min;
                    for (std::size_t c = 0; c < col_count; ++c)
                        col_w[c] = min_w[c] + (max_w[c] - min_w[c]) *
                                              (elastic > 0.0 ? space / elastic : 0.0);
                }
                b.col_widths = col_w;

                // Cell heights from wrapped measurements; rows share the tallest cell.
                for (std::size_t r = 0; r < cells.size(); ++r) {
                    Block::Row row;
                    for (std::size_t c = 0; c < col_count; ++c) {
                        Block::Cell cell;
                        if (c < cells[r].size()) {
                            cell.run = cells[r][c].runs;
                            cell.align = cells[r][c].align;
                            cell.header = cells[r][c].header;
                        }
                        const auto m = measure_runs(cell.run, col_w[c] - 2.0 * cpad, style);
                        cell.height = m.height + 2.0 * cpad;
                        row.cells.push_back(std::move(cell));
                    }
                    double row_h = row.cells.empty() ? 0.0 : row.cells[0].height;
                    for (const auto& cell : row.cells) row_h = std::max(row_h, cell.height);
                    row.height = row_h;
                    b.rows.push_back(std::move(row));
                }

                double total = 0.0;
                for (const auto& row : b.rows) total += row.height;
                b.height = total;
                b.baseline = cpad * 0.5;
                // Center the table block itself within the content area; the
                // draw layer shifts all table geometry by table_center_x.
                double table_w = 0.0;
                for (double cw : b.col_widths) table_w += cw;
                b.content_width = table_w;
                b.table_center_x = std::max(0.0, (content_width_pt - table_w) / 2.0);
                b.y = y + b.margin_before;
                y = b.y + b.height + b.margin_after;
                push_block(std::move(b));
                return;
            }
            case NodeType::Toc: {
                if (!render_toc) {
                    Block b;
                    b.kind = Block::Kind::P;
                    const auto pbox = style.box_for(StyleSelector::Paragraph);
                    b.margin_before = pbox.margin_top_pt;
                    b.margin_after = pbox.margin_bottom_pt;
                    StyledText t;
                    t.text = "[[TOC]]";
                    t.font_family = style.base_font;
                    t.size_pt = style.base_font_pt;
                    t.color = style.text_color;
                    b.run = {t};
                    const auto m = measure_runs(b.run, content_width_pt, style);
                    b.content_width = content_width_pt;
                    b.height = m.height;
                    b.baseline = m.baseline_pt;
                    b.y = y + b.margin_before;
                    y = b.y + b.height + b.margin_after;
                    push_block(std::move(b));
                    return;
                }
                const auto headings = ast.headings();
                std::vector<int> counters;
                for (const auto& h : headings) {
                    Block b;
                    b.kind = Block::Kind::TocRow;
                    b.toc_anchor = h.anchor;
                    b.toc_text = h.text;
                    b.toc_indent = static_cast<double>(std::max(0, h.level - 1)) * 14.0;
                    counters.resize(static_cast<std::size_t>(h.level));
                    ++counters.back();
                    std::string num;
                    for (int c : counters) {
                        if (!num.empty()) num += ".";
                        num += std::to_string(c);
                    }
                    b.toc_number = num;
                    const auto tbox = style.box_for(StyleSelector::Toc);
                    b.margin_before = tbox.margin_top_pt;
                    b.margin_after = tbox.margin_bottom_pt;
                    b.has_link_hit = true;
                    b.link_href = "#" + h.anchor;

                    std::vector<StyledText> runs;
                    StyledText num_run;
                    num_run.text = num + ". ";
                    num_run.font_family = style.base_font;
                    num_run.size_pt = style.base_font_pt;
                    num_run.color = style.text_color;
                    num_run.underline = false;
                    runs.push_back(num_run);
                    StyledText txt;
                    txt.text = h.text;
                    txt.font_family = style.base_font;
                    txt.size_pt = style.base_font_pt;
                    txt.color = style.color_for(StyleSelector::TocLink);
                    txt.underline = false;
                    runs.push_back(txt);
// Optional trailing page number (PDF two-pass layout).
                    // Measured separately so the title+leader wrap width is
                    // unaffected; the draw layer right-aligns this last run.
                    double page_num_w = 0.0;
                    if (toc_pages) {
                        if (const auto pg = toc_pages(h.anchor)) {
                            b.toc_has_page = true;
                            b.toc_page = *pg;
                            StyledText pg_run;
                            pg_run.text = std::to_string(*pg);
                            pg_run.font_family = style.base_font;
                            pg_run.size_pt = style.base_font_pt;
                            pg_run.color = style.text_color;
                            pg_run.underline = false;
                            runs.push_back(std::move(pg_run));
                        }
                    }
                    // Measure the title runs (all but the last page-number run)
                    // at the full available width; the page number itself is
                    // tiny so it must never steal wrap width from the title.
                    const std::size_t title_n = toc_pages && b.toc_has_page
                                                    ? runs.size() - 1 : runs.size();
                    std::vector<StyledText> title_runs(runs.begin(),
                                                       runs.begin() + static_cast<std::ptrdiff_t>(title_n));
                    double avail = content_width_pt - b.toc_indent;
                    PangoMetrics m;
                    if (!title_runs.empty())
                        m = measure_runs(title_runs, avail, style);
                    if (b.toc_has_page)
                        page_num_w = measure_runs(
                            std::vector<StyledText>{runs.back()}, -1.0, style).width;
                    b.page_num_w = page_num_w;
                    b.run = std::move(runs);
                    b.content_width = avail;
                    b.height = m.height;
                    b.baseline = m.baseline_pt;
                    b.y = y + b.margin_before;
                    y = b.y + b.height + b.margin_after;
                    push_block(std::move(b));
                }
                return;
            }
            case NodeType::HtmlBlock:
                if (is_page_break_marker(n.text)) {
                    // A page-break marker is a small, nearly-invisible block:
                    // the preview draws a subtle dashed line on it, the PDF
                    // paginator treats it as a hard page boundary. It must
                    // never push content far apart in the continuous preview.
                    Block b;
                    b.kind = Block::Kind::PageBreak;
                    b.margin_before = 6.0;
                    b.margin_after = 6.0;
                    b.height = 2.0;
                    b.content_width = content_width_pt;
                    b.color = style.text_color;
                    b.y = y + b.margin_before;
                    y = b.y + b.height + b.margin_after;
                    push_block(std::move(b));
                    return;
                }
                {
                    const std::string t = trim_ws(n.text);
                    const auto tag = parse_html_tag(t);
                    if (tag && tag->consumed == t.size() &&
                        is_block_level_tag(tag->name)) {
                        if (tag->closing) {
                            // Case A close: pop the wrapper if one is open.
                            if (!block_ctx.empty()) block_ctx.pop_back();
                        } else if (!tag->self_closing) {
                            // Case A open: open a wrapper context for the
                            // following markdown paragraphs.
                            block_ctx.push_back(
                                style_from_tag(tag->name, tag->style, style));
                        }
                        return;
                    }
                    // Case B/C/D: one whole raw chunk -> mini inline renderer.
                    std::vector<StyledText> runs;
                    if (render_html_block_runs(n.text, style, style.base_font,
                                               style.base_font_pt, style.text_color,
                                               runs)) {
                        if (runs.empty()) return;
                        Block b;
                        b.kind = Block::Kind::P;
                        b.margin_before = style.box_for(StyleSelector::Paragraph).margin_top_pt;
                        b.margin_after = style.box_for(StyleSelector::Paragraph).margin_bottom_pt;
                        b.run = std::move(runs);
                        const auto m = measure_runs(b.run, content_width_pt, style);
                        b.content_width = content_width_pt;
                        b.height = m.height;
                        b.baseline = m.baseline_pt;
                        b.y = y + b.margin_before;
                        y = b.y + b.height + b.margin_after;
                        push_block(std::move(b));
                    }
                    return;
                }
            default:
                for (const Node& c : n.children) emit_node(c);
                return;
        }
    };

    for (const Node& c : root.children) emit_node(c);
    res.total_height = y;
    return res;
}

} // namespace remin::markdown