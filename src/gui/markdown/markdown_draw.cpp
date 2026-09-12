#include "gui/markdown/markdown_draw.hpp"

#include "gui/markdown/markdown_pango.hpp"

#include <cairo/cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <cstring>

namespace remin::markdown {

namespace {

// Copy a GdkPixbuf into an ARGB32 Cairo image surface at its current size.
// The pixel channel order (RGBA on disk) is swapped to Cairo's BGRA with
// premultiplied alpha so `paint()` composes correctly.
Cairo::RefPtr<Cairo::ImageSurface> pixbuf_to_surface(GdkPixbuf* pb) {
    const int sw = gdk_pixbuf_get_width(pb);
    const int sh = gdk_pixbuf_get_height(pb);
    if (sw <= 0 || sh <= 0) return {};
    const int rowstride = gdk_pixbuf_get_rowstride(pb);
    const int nch = gdk_pixbuf_get_n_channels(pb);
    const bool has_alpha = gdk_pixbuf_get_has_alpha(pb);

    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, sw, sh);
    if (!surface->get_data()) return {};
    const guint8* src = gdk_pixbuf_get_pixels(pb);
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
    return surface;
}

// Draw a pixbuf at (x,y) scaled to (w,h) with premultiplied alpha for Cairo.
// On a PDF target the image keeps its native resolution and Cairo scales the
// painting with a transform, so the PDF embeds the full-resolution pixels
// (crisp when zoomed — same trade-off as the HTML `<img src>`). The on-screen
// path keeps the old behavior: pre-scale bilinear, blit 1:1.
void draw_pixbuf(const Cairo::RefPtr<Cairo::Context>& cr, GdkPixbuf* source,
                 double x, double y, double w, double h) {
    if (!source) return;
    const bool pdf_target =
        cr->get_target() && cr->get_target()->get_type() == Cairo::Surface::Type::PDF;

    if (!pdf_target) {
        GdkPixbuf* scaled = gdk_pixbuf_scale_simple(
            source, static_cast<int>(std::max(1.0, w)),
            static_cast<int>(std::max(1.0, h)), GDK_INTERP_BILINEAR);
        if (!scaled) return;
        auto surface = pixbuf_to_surface(scaled);
        g_object_unref(scaled);
        if (!surface) return;
        cr->save();
        cr->set_source(surface, x, y);
        cr->paint();
        cr->restore();
        return;
    }

    auto surface = pixbuf_to_surface(source);
    if (!surface) return;
    const double nw = gdk_pixbuf_get_width(source);
    const double nh = gdk_pixbuf_get_height(source);
    if (nw <= 0.0 || nh <= 0.0) return;
    cr->save();
    cr->translate(x, y);
    cr->scale(w / nw, h / nh);
    cr->set_source(surface, 0.0, 0.0);
    cr->paint();
    cr->restore();
}

// PDF named-link/destination helpers (cairo's C tag API — cairomm does not
// wrap cairo_tag_begin/end). Attribute strings must survive only for the
// duration of the call; begin/end bracket the drawing ops that form the
// clickable/target rectangle.
void tag_begin(const Cairo::RefPtr<Cairo::Context>& cr, const std::string& name,
               const std::string& attrs) {
    cairo_tag_begin(cr->cobj(), name.c_str(), attrs.c_str());
}
void tag_end(const Cairo::RefPtr<Cairo::Context>& cr, const std::string& name) {
    cairo_tag_end(cr->cobj(), name.c_str());
}

// The anchor of a block whose href is an internal "#fragment"; empty otherwise.
std::string internal_anchor(const Block& b) {
    if (b.link_href.size() > 1 && b.link_href[0] == '#') return b.link_href.substr(1);
    return {};
}

// Open the PDF tag appropriate for the block (`emit_links` only). Headings
// become *destinations* (link targets); other linked blocks become Links.
// External links (http/https) use `uri` attribute; internal anchors use `dest`.
void open_link_tags(const Cairo::RefPtr<Cairo::Context>& cr, const Block& b,
                    bool emit_links) {
    if (!emit_links || !b.has_link_hit) return;
    if (b.kind == Block::Kind::Heading) {
        const std::string anchor = internal_anchor(b);
        if (!anchor.empty())
            tag_begin(cr, CAIRO_TAG_DEST, "name='" + anchor + "'");
        return;
    }
    // Non-heading link: could be internal (#anchor) or external (http(s)://).
    if (b.link_href.rfind("http", 0) == 0) {
        tag_begin(cr, CAIRO_TAG_LINK, "uri='" + b.link_href + "'");
    } else if (!b.link_href.empty() && b.link_href[0] == '#') {
        tag_begin(cr, CAIRO_TAG_LINK, "dest='" + b.link_href.substr(1) + "'");
    }
}

void close_link_tags(const Cairo::RefPtr<Cairo::Context>& cr, const Block& b,
                     bool emit_links) {
    if (!emit_links || !b.has_link_hit) return;
    if (b.kind == Block::Kind::Heading) {
        const std::string anchor = internal_anchor(b);
        if (!anchor.empty())
            tag_end(cr, CAIRO_TAG_DEST);
        return;
    }
    if (b.link_href.rfind("http", 0) == 0 || (!b.link_href.empty() && b.link_href[0] == '#')) {
        tag_end(cr, CAIRO_TAG_LINK);
    }
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
                       const StyleSheet& style, bool justify,
                       bool emit_links) {
    for (std::size_t idx = begin; idx < end; ++idx) {
        const Block& b = blocks[idx];
        const double y = b.y;

        // Block background / quote bar. Tables are centered via
        // table_center_x, so any block-level box must share that offset too
        // (otherwise the outline stays at x=0 while the grid is shifted).
        const double box_x = b.is_table ? b.table_center_x : 0.0;
        if (b.has_bg) {
            cr->save();
            set_color(cr, b.bg);
            cr->rectangle(box_x, y, b.content_width + 2.0 * b.padding,
                          b.height + (b.border_w > 0 ? 0.0 : 0.0));
            cr->fill();
            cr->restore();
        }
        if (b.border_w > 0.0) {
            cr->save();
            set_color(cr, b.outer_border);
            cr->set_line_width(b.border_w);
            cr->rectangle(box_x, y, b.content_width + 2.0 * b.padding, b.height);
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
                // Table grid + cells. All geometry is shifted by the centering
                // offset computed during layout (shared by preview + PDF).
                const double off_x = b.table_center_x;
                const double pad = b.cell_pad > 0.0 ? b.cell_pad : 3.0;
                const double table_w = b.content_width;
                double row_y = y;
                for (const Block::Row& row : b.rows) {
                    double col_x = off_x;
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
                            const auto m = measure_runs(cell.run, cw - 2.0 * pad, style);
                            auto layout = Pango::Layout::create(cr);
                            apply_styled_runs(*layout, cell.run, cw - 2.0 * pad, style);
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
                // Grid: outer box + row separators + column separators.
                cr->save();
                set_color(cr, b.outer_border);
                cr->set_line_width(1.0);
                row_y = y;
                for (const Block::Row& row : b.rows) {
                    cr->move_to(off_x, row_y);
                    cr->line_to(off_x + table_w, row_y);
                    row_y += row.height;
                }
                cr->move_to(off_x, row_y);
                cr->line_to(off_x + table_w, row_y);
                // Vertical strokes: left edge, each column separator, right edge.
                // Drawn on top of the header fill so every band gets a full,
                // uniform border (the generic block outline is disabled for
                // tables).
                double col_x = off_x;
                for (double cw : b.col_widths) {
                    cr->move_to(col_x, y);
                    cr->line_to(col_x, row_y);
                    col_x += cw;
                }
                cr->move_to(col_x, y);
                cr->line_to(col_x, row_y);
                cr->stroke();
                // Emphasize the header underline (below the first table row).
                if (!b.rows.empty()) {
                    const double header_bottom = y + b.rows.front().height;
                    cr->set_line_width(1.6);
                    cr->move_to(off_x, header_bottom);
                    cr->line_to(off_x + table_w, header_bottom);
                    cr->stroke();
                }
                cr->restore();
                continue;
            }
            case Block::Kind::PageBreak: {
                // A subtle dashed line marking a forced page break (preview).
                // The PDF paginator never feeds these blocks to the renderer.
                const double yc = y + b.height / 2.0;
                cr->save();
                Color c = b.color;
                c.a *= 0.40f;
                set_color(cr, c);
                cr->set_line_width(1.0);
                cr->set_dash(std::vector<double>{3.0, 3.0}, 0.0);
                cr->move_to(0.0, yc);
                cr->line_to(b.content_width, yc);
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
            text_y = y + b.padding + (b.kind == Block::Kind::Code ? b.badge_room : 0.0);
        }
        // Code fence language badge (top-right corner).
        if (b.kind == Block::Kind::Code && !b.code_lang.empty()) {
            const double badge_pad = kCodeBadgePad;
            const double badge_font_pt = kCodeBadgeFontPt;
            auto badge_layout = Pango::Layout::create(cr);
            Pango::FontDescription fd(style.base_font);
            fd.set_absolute_size(static_cast<int>(badge_font_pt * PANGO_SCALE));
            badge_layout->set_font_description(fd);
            badge_layout->set_text(b.code_lang);
            int bw, bh;
            badge_layout->get_pixel_size(bw, bh);
            const double badge_w = static_cast<double>(bw) + 2.0 * badge_pad;
            const double badge_x = b.content_width + 2.0 * b.padding - badge_w - badge_pad;
            const double badge_y = y + badge_pad;
            // Badge text only (no background).
            cr->save();
            Color fg = style.text_color;
            fg.a *= 0.6f;
            set_color(cr, fg);
            cr->move_to(badge_x, badge_y);
            badge_layout->show_in_cairo_context(cr);
            cr->restore();
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
            if (b.toc_has_page && b.run.size() >= 2) {
                // PDF TOC row: title + dotted leader + right-aligned page
                // number. The page number lives in the last run; everything
                // before it is the clickable title.
                open_link_tags(cr, b, emit_links);
                const std::size_t title_n = b.run.size() - 1;
                const std::vector<StyledText> title_runs(
                    b.run.begin(), b.run.begin() + static_cast<std::ptrdiff_t>(title_n));
                const auto tm = measure_runs(title_runs, wrap_w, style);
                auto title_layout = Pango::Layout::create(cr);
                apply_styled_runs(*title_layout, title_runs, wrap_w, style);
                cr->save();
                cr->move_to(text_x, text_y);
                title_layout->show_in_cairo_context(cr);
                cr->restore();
                close_link_tags(cr, b, emit_links);

                // Right edge of the content column (b.content_width already
                // excludes toc_indent, so add it back).
                const double page_right = b.content_width + b.toc_indent;
                const double pn_w = std::max(0.0, b.page_num_w);
                const double gap = 6.0;
                const double leader_right = page_right - pn_w - gap;
                const double leader_left = text_x + tm.width + gap;
                if (leader_right > leader_left + 4.0) {
                    const double leader_y = y + b.baseline - 0.6;
                    cr->save();
                    Color leader_c = style.text_color;
                    leader_c.a *= 0.35f;
                    set_color(cr, leader_c);
                    cr->set_line_width(0.9);
                    cr->set_dash(std::vector<double>{1.4, 3.4}, 0.0);
                    cr->move_to(leader_left, leader_y);
                    cr->line_to(leader_right, leader_y);
                    cr->stroke();
                    cr->restore();
                }

                const StyledText pn_run = b.run.back();
                const auto pnm = measure_runs(std::vector<StyledText>{pn_run}, -1.0, style);
                auto pn_layout = Pango::Layout::create(cr);
                apply_styled_runs(*pn_layout, std::vector<StyledText>{pn_run}, -1.0, style);
                cr->save();
                cr->move_to(page_right - pnm.width, text_y);
                pn_layout->show_in_cairo_context(cr);
                cr->restore();
                continue;
            }
        }

        if (b.run.empty() || wrap_w <= 0.0) continue;

        open_link_tags(cr, b, emit_links);
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
        close_link_tags(cr, b, emit_links);
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