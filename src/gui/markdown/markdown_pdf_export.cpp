#include "gui/markdown/markdown_pdf_export.hpp"

#include "gui/markdown/markdown_draw.hpp"
#include "gui/markdown/markdown_pango.hpp"

#include <cairomm/cairomm.h>

#include <ctime>
#include <sstream>
#include <vector>

namespace remin::markdown {

namespace {

// One horizontal line (left/center/right) drawn at a fixed baseline.
void draw_band(const Cairo::RefPtr<Cairo::Context>& cr, const StyleSheet& style,
               const std::string& left, const std::string& center,
               const std::string& right, double page_left, double page_right,
               double baseline_y) {
    const auto make = [&](const std::string& t) {
        StyledText r;
        r.text = t;
        r.font_family = style.base_font;
        r.size_pt = 8.5;
        r.color = style.text_color;
        r.underline = false;
        return r;
    };
    const auto draw_side = [&](const std::string& t, double x, bool center_align) {
        if (t.empty()) return;
        auto layout = Pango::Layout::create(cr);
        apply_styled_runs(*layout, std::vector<StyledText>{make(t)}, -1.0, style);
        int w = 0, h = 0;
        layout->get_size(w, h);
        double tx = x;
        if (center_align) tx = x - static_cast<double>(w) / PANGO_SCALE / 2.0;
        cr->save();
        cr->move_to(tx, baseline_y - static_cast<double>(layout->get_baseline()) / PANGO_SCALE);
        layout->show_in_cairo_context(cr);
        cr->restore();
    };
    draw_side(left, page_left, false);
    draw_side(center, (page_left + page_right) / 2.0, true);
    draw_side(right, page_right, true);
}

std::string expand_tokens(const std::string& in, int page, int pages,
                          const PdfPageMeta& meta) {
    std::string out = in;
    const auto rep1 = [&](const std::string& tok, const std::string& val) {
        std::size_t p = 0;
        while ((p = out.find(tok, p)) != std::string::npos) {
            out.replace(p, tok.size(), val);
            p += val.size();
        }
    };
    rep1("{page}", std::to_string(page));
    rep1("{pages}", std::to_string(pages));

    std::tm tm;
    const std::time_t now = std::time(nullptr);
    localtime_r(&now, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    rep1("{date}", buf);
    std::strftime(buf, sizeof(buf), "%H:%M", &tm);
    rep1("{time}", buf);

    rep1("{title}", meta.title);
    rep1("{author}", meta.author);
    rep1("{filename}", meta.title);
    return out;
}

} // namespace

bool export_pdf(const MarkdownAst& ast, const StyleSheet& style,
                const ImageResolver& resolve_image,
                const std::filesystem::path& out_path,
                const PdfPageMeta& meta) {
    const double page_w = style.page_width_pt;
    const double page_h = style.page_height_pt;
    const double margin = style.page_margin_pt;
    const double content_left = margin;
    const double content_right = page_w - margin;
    const double content_w = content_right - content_left;
    // Header/footer bands live inside the margins.
    const double top_anchor = margin * 0.55;
    const double bottom_anchor = page_h - margin * 0.55;
    const double content_top = margin;
    const double content_bottom = page_h - margin;

    const LayoutResult layout = layout_document(ast, style, content_w, resolve_image);
    if (layout.blocks.empty()) return false;

    // ---- paginate into page slices ----------------------------------------
    struct PageSlice {
        std::size_t first;
        std::size_t last;       // included index
        double start_y;
    };
    std::vector<PageSlice> pages;
    std::size_t cursor = 0;
    double page_top = layout.blocks[0].y;  // absolute y of this page's first block
    while (cursor < layout.blocks.size()) {
        std::size_t last = cursor;
        const std::size_t n = layout.blocks.size();
        // find the longest consecutive run fitting the page
        std::size_t candidate = cursor;
        while (candidate < n) {
            const Block& b = layout.blocks[candidate];
            const double rel_top = b.y - page_top;
            const double rel_bottom = rel_top + b.height;
            if (rel_bottom > content_bottom - content_top) {
                if (candidate == cursor) {
                    // block taller than a full page: force it in (clipped) once.
                    last = candidate;
                    candidate = n;
                    break;
                }
                break;
            }
            last = candidate;
            ++candidate;
            // heading keep-with-next: require the next block to fit too
            if (candidate < n && layout.block_keeps_with_next(b)) {
                const Block& nb = layout.blocks[candidate];
                const double nb_rel_top = nb.y - page_top;
                if (nb_rel_top + nb.height > content_bottom - content_top) {
                    // Next block doesn't fit: end this page *before* the
                    // heading so heading + content move together.
                    if (last > cursor) --last;
                    break;
                }
            }
        }
        pages.push_back(PageSlice{cursor, last, page_top});
        // advance
        std::size_t next = last + 1;
        if (next < n) {
            page_top = layout.blocks[next].y;
        }
        cursor = next;
    }
    if (pages.empty())
        pages.push_back(PageSlice{0, 0, 0.0});

    const int total_pages = static_cast<int>(pages.size());

    // ---- render ------------------------------------------------------------
    Cairo::RefPtr<Cairo::PdfSurface> surface;
    try {
        surface = Cairo::PdfSurface::create(out_path.string(), page_w, page_h);
    } catch (const std::exception&) {
        return false;  // unwritable path / Cairo error
    }
    if (!surface) return false;
    auto cr = Cairo::Context::create(surface);

    for (int pi = 0; pi < total_pages; ++pi) {
        const PageSlice& slice = pages[static_cast<std::size_t>(pi)];
        // content
        cr->save();
        cr->translate(-content_left, -(slice.start_y - content_top));
        draw_blocks_range(cr, layout.blocks, slice.first, slice.last + 1, style);
        cr->restore();

        // header / footer
        const std::string hleft = expand_tokens(meta.header_left, pi, total_pages, meta);
        const std::string hcenter = expand_tokens(meta.header_center, pi, total_pages, meta);
        const std::string hright = expand_tokens(meta.header_right, pi, total_pages, meta);
        const std::string fleft = expand_tokens(meta.footer_left, pi, total_pages, meta);
        const std::string fcenter = expand_tokens(meta.footer_center, pi, total_pages, meta);
        const std::string fright = expand_tokens(meta.footer_right, pi, total_pages, meta);

        draw_band(cr, style, hleft, hcenter, hright, content_left, content_right, top_anchor);
        draw_band(cr, style, fleft, fcenter, fright, content_left, content_right, bottom_anchor);

        if (pi + 1 < total_pages) surface->show_page();
    }
    surface->finish();
    return true;
}

} // namespace remin::markdown