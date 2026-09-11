#include "gui/markdown/markdown_draw.hpp"

#include "gui/markdown/markdown_pango.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <cstring>

namespace remin::markdown {

namespace {

// Draw a pixbuf at (x,y) scaled to (w,h) with premultiplied alpha for Cairo.
void draw_pixbuf(const Cairo::RefPtr<Cairo::Context>& cr, GdkPixbuf* source,
                 double x, double y, double w, double h) {
    if (!source) return;
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(
        source, static_cast<int>(std::max(1.0, w)), static_cast<int>(std::max(1.0, h)),
        GDK_INTERP_BILINEAR);
    if (!scaled) return;
    const int sw = gdk_pixbuf_get_width(scaled);
    const int sh = gdk_pixbuf_get_height(scaled);
    const int rowstride = gdk_pixbuf_get_rowstride(scaled);
    const int nch = gdk_pixbuf_get_n_channels(scaled);
    const bool has_alpha = gdk_pixbuf_get_has_alpha(scaled);

    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, sw, sh);
    if (surface->get_data()) {
        const guint8* src = gdk_pixbuf_get_pixels(scaled);
        guint8* dst = surface->get_data();
        const int dstride = surface->get_stride();
        for (int y2 = 0; y2 < sh; ++y2) {
            const guint8* row = src + y2 * rowstride;
            guint8* outrow = dst + y2 * dstride;
            for (int x2 = 0; x2 < sw; ++x2) {
                const guint8* p = row + x2 * nch;
                guint8* o = outrow + x2 * 4;
                if (has_alpha) {
                    const guint8 a = p[3];
                    o[0] = static_cast<guint8>((static_cast<unsigned>(p[2]) * a) / 255);  // b
                    o[1] = static_cast<guint8>((static_cast<unsigned>(p[1]) * a) / 255);  // g
                    o[2] = static_cast<guint8>((static_cast<unsigned>(p[0]) * a) / 255);  // r
                    o[3] = a;
                } else {
                    o[0] = p[2];
                    o[1] = p[1];
                    o[2] = p[0];
                    o[3] = 0xFF;
                }
            }
        }
        surface->mark_dirty();
    }
    cr->save();
    cr->set_source(surface, x, y);
    cr->paint();
    cr->restore();
    g_object_unref(scaled);
}

void set_color(const Cairo::RefPtr<Cairo::Context>& cr, const Color& c) {
    if (!c.valid) return;
    // Pre-multiply alpha into RGB for cairo source.
    cr->set_source_rgba(c.r, c.g, c.b, c.a);
}

double text_x_for_align(const Block::Cell& cell, double col_x, double col_w,
                        double text_w, double pad) {
    switch (cell.align) {
        case CellAlign::Right: return col_x + col_w - pad - text_w;
        case CellAlign::Center: return col_x + (col_w - text_w) / 2.0;
        default: return col_x + pad;
    }
}

} // namespace

void draw_flow(const Cairo::RefPtr<Cairo::Context>& cr,
               const std::vector<Block>& blocks,
               const StyleSheet& style) {
    draw_blocks_range(cr, blocks, 0, blocks.size(), style);
}

void draw_blocks_range(const Cairo::RefPtr<Cairo::Context>& cr,
                       const std::vector<Block>& blocks,
                       std::size_t begin, std::size_t end,
                       const StyleSheet& style, bool justify) {
    for (std::size_t idx = begin; idx < end; ++idx) {
        const Block& b = blocks[idx];
        const double y = b.y;

        // Block background / quote bar.
        if (b.has_bg) {
            cr->save();
            set_color(cr, b.bg);
            cr->rectangle(0.0, y, b.content_width + 2.0 * b.padding,
                          b.height + (b.border_w > 0 ? 0.0 : 0.0));
            cr->fill();
            cr->restore();
        }
        if (b.border_w > 0.0) {
            cr->save();
            set_color(cr, b.outer_border);
            cr->set_line_width(b.border_w);
            cr->rectangle(0.0, y, b.content_width + 2.0 * b.padding, b.height);
            cr->stroke();
            cr->restore();
        }
        if (b.border_left_w > 0.0) {
            cr->save();
            set_color(cr, b.border);
            cr->set_line_width(b.border_left_w);
            cr->move_to(0.0, y);
            cr->line_to(0.0, y + b.height);
            cr->stroke();
            cr->restore();
        }

        switch (b.kind) {
            case Block::Kind::Hr: {
                cr->save();
                set_color(cr, b.color);
                cr->set_line_width(1.0);
                cr->move_to(0.0, y);
                cr->line_to(b.content_width, y);
                cr->stroke();
                cr->restore();
                continue;
            }
            case Block::Kind::Image:
                if (b.has_image) {
                    GError* err = nullptr;
                    GdkPixbuf* pb = gdk_pixbuf_new_from_file(b.image_path.c_str(), &err);
                    if (!pb) {
                        if (err) g_error_free(err);
                        continue;
                    }
                    draw_pixbuf(cr, pb, 0.0, y, b.image_w, b.image_h);
                    g_object_unref(pb);
                }
                continue;
            case Block::Kind::Table: {
                // Table grid + cells.
                double row_y = y;
                const double pad = 3.0;
                for (const Block::Row& row : b.rows) {
                    double col_x = 0.0;
                    for (std::size_t c = 0; c < row.cells.size() && c < b.col_widths.size();
                         ++c) {
                        const Block::Cell& cell = row.cells[c];
                        const double cw = b.col_widths[c];
                        // Header background.
                        if (cell.header) {
                            cr->save();
                            set_color(cr, b.bg);
                            cr->rectangle(col_x, row_y, cw, row.height);
                            cr->fill();
                            cr->restore();
                        }
                        if (!cell.run.empty()) {
                            const auto m = measure_runs(cell.run, cw, style);
                            auto layout = Pango::Layout::create(cr);
                            apply_styled_runs(*layout, cell.run, cw, style);
                            const double tx = text_x_for_align(cell, col_x, cw, m.width, pad);
                            cr->save();
                            cr->move_to(tx, row_y + pad);
                            layout->show_in_cairo_context(cr);
                            cr->restore();
                        }
                        col_x += cw;
                    }
                    row_y += row.height;
                }
                // Grid lines.
                cr->save();
                set_color(cr, b.outer_border);
                cr->set_line_width(1.0);
                row_y = y;
                for (const Block::Row& row : b.rows) {
                    cr->move_to(0.0, row_y);
                    cr->line_to(b.content_width, row_y);
                    row_y += row.height;
                }
                cr->move_to(0.0, row_y);
                cr->line_to(b.content_width, row_y);
                double col_x = 0.0;
                for (double cw : b.col_widths) {
                    col_x += cw;
                    cr->move_to(col_x, y);
                    cr->line_to(col_x, row_y);
                }
                cr->stroke();
                cr->restore();
                continue;
            }
            default:
                break;  // text blocks below
        }

        // Text blocks (P, Heading, Quote, code, list/task items, toc rows).
        double text_y = y;
        double text_x = 0.0;
        double text_w = b.content_width;
        double wrap_w = b.content_width;

        if (b.kind == Block::Kind::Code || b.kind == Block::Kind::Quote) {
            text_x = b.padding;
            text_y = y + b.padding;
        }
        if (b.kind == Block::Kind::UlItem || b.kind == Block::Kind::OlItem ||
            b.kind == Block::Kind::TaskItem) {
            StyledText m;
            m.text = b.marker;
            m.font_family = style.base_font;
            m.size_pt = style.base_font_pt;
            m.color = style.color_for(StyleSelector::ListItem);
            const auto mm = measure_runs(std::vector<StyledText>{m}, -1.0, style);
            auto marker_layout = Pango::Layout::create(cr);
            apply_styled_runs(*marker_layout, std::vector<StyledText>{m}, -1.0, style);
            text_x = b.indent_pt;
            cr->save();
            cr->move_to(text_x, y);
            marker_layout->show_in_cairo_context(cr);
            cr->restore();
            // Text area already excluded the marker width during layout; just
            // skip past the measured marker now.
            text_x = b.indent_pt + mm.width + 4.0;
            text_w = b.content_width;
            wrap_w = text_w;
        }
        if (b.kind == Block::Kind::TocRow) {
            text_x = b.toc_indent;
            text_w = b.content_width - b.toc_indent;
            wrap_w = text_w;
        }

        if (b.run.empty() || wrap_w <= 0.0) continue;

        auto layout = Pango::Layout::create(cr);
        apply_styled_runs(*layout, b.run, wrap_w, style);
        if (justify) {
            layout->set_justify(true);
            layout->set_alignment(Pango::Alignment::LEFT);
        }
        cr->save();
        cr->move_to(text_x, text_y);
        layout->show_in_cairo_context(cr);
        cr->restore();
    }
}

std::vector<HitRegion> collect_hit_regions(const std::vector<Block>& blocks) {
    std::vector<HitRegion> out;
    for (const Block& b : blocks) {
        if (!b.has_link_hit || b.link_href.empty()) continue;
        double x = 0.0;
        double y = b.y + b.baseline * 0.3;
        double h = b.height;
        double w = b.link_w > 0.0 ? b.link_w : b.content_width;
        if (b.kind == Block::Kind::TocRow) {
            x = b.toc_indent;
            w = b.content_width - b.toc_indent;
        }
        out.push_back(HitRegion{x, y, w, h, b.link_href});
    }
    return out;
}

} // namespace remin::markdown