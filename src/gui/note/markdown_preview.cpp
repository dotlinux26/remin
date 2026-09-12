#include "gui/note/markdown_preview.hpp"

#include "gui/markdown/markdown_ast.hpp"
#include "gui/markdown/markdown_layout.hpp"
#include "gui/markdown/markdown_draw.hpp"

#include <giomm.h>
#include <pangomm.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace remin::gui {

using namespace remin::markdown;

namespace {

// Horizontal breathing room for the on-screen preview (the PDF exporter uses
// the stylesheet's own page margin). Just a little inset so the content never
// touches the split divider / window edge.
constexpr double kPreviewMargin = 20.0;

std::optional<std::filesystem::path> resolve_local_image(
    const std::string& ref, const std::filesystem::path& asset_dir,
    const std::filesystem::path& note_dir) {
    if (ref.empty()) return std::nullopt;
    if (ref.rfind("http", 0) == 0) return std::nullopt;
    if (ref.rfind("data:", 0) == 0) return std::nullopt;

    std::filesystem::path candidate;
    if (ref.rfind("remin://images/", 0) == 0) {
        const char* home = std::getenv("HOME");
        if (!home) return std::nullopt;
        candidate = std::filesystem::path(home) / "remin-image" / ref.substr(15);
    } else if (ref.rfind("assets/", 0) == 0) {
        const std::string rel = ref.substr(7);
        candidate = asset_dir.empty() ? (note_dir / "assets") : asset_dir;
        candidate /= rel;
    } else if (std::filesystem::path(ref).is_absolute()) {
        candidate = std::filesystem::path(ref);
    } else if (!note_dir.empty()) {
        candidate = note_dir / ref;
    } else {
        candidate = std::filesystem::path(ref);
    }
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec)) {
        const auto c = std::filesystem::canonical(candidate, ec);
        if (!ec) return c;
        return candidate;
    }
    return std::nullopt;
}

} // namespace

MarkdownPreview::MarkdownPreview() {
    set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    set_hexpand(true);
    set_vexpand(true);

    canvas_ = Gtk::make_managed<Gtk::DrawingArea>();
    canvas_->set_hexpand(true);
    canvas_->set_draw_func(sigc::mem_fun(*this, &MarkdownPreview::on_canvas_draw));
    set_child(*canvas_);

    style_ = default_style(palette_);

    // Link navigation + hand cursor over links.
    auto click = Gtk::GestureClick::create();
    click->set_button(1);
    click->signal_pressed().connect([this](int n, double x, double y) {
        if (n != 1) return;
        const double margin = kPreviewMargin;
        const double vis = get_vadjustment() ? get_vadjustment()->get_value() : 0.0;
        const double fx = x - margin;
        const double fy = y + vis - margin;
        for (const auto& r : collect_hit_regions(layout_.blocks)) {
            if (!(fx >= r.x && fx <= r.x + r.w && fy >= r.y && fy <= r.y + r.h))
                continue;
            if (r.href.rfind("http", 0) == 0) {
                Gio::AppInfo::launch_default_for_uri(r.href);
            } else if (r.href.rfind('#', 0) == 0 && r.href.size() > 1) {
                const std::string target = r.href.substr(1);
                const double mt = style_.page_margin_pt;
                for (const Block& b : layout_.blocks)
                    for (const StyledText& run : b.run)
                        if (!run.anchor.empty() && run.anchor == target) {
                            if (auto v = get_vadjustment())
                                v->set_value(b.y + mt);
                            return;
                        }
            }
            return;
        }
    });
    canvas_->add_controller(click);

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) {
        const double margin = kPreviewMargin;
        const double vis = get_vadjustment() ? get_vadjustment()->get_value() : 0.0;
        const double fx = x - margin;
        const double fy = y + vis - margin;
        bool over = false;
        for (const auto& r : collect_hit_regions(layout_.blocks))
            if (fx >= r.x && fx <= r.x + r.w && fy >= r.y && fy <= r.y + r.h) {
                over = true;
                break;
            }
        if (over != cursor_over_link_) {
            cursor_over_link_ = over;
            if (over) canvas_->set_cursor("pointer");
            else canvas_->set_cursor();
        }
    });
    canvas_->add_controller(motion);
}

MarkdownPreview::~MarkdownPreview() = default;

void MarkdownPreview::set_note_dir(const std::string& note_dir,
                                   const std::string& asset_dir) {
    note_dir_ = note_dir;
    asset_dir_ = asset_dir;
    if (!asset_dir_.empty()) {
        resolved_asset_dir_ = std::filesystem::path(asset_dir_);
    } else if (!note_dir_.empty()) {
        resolved_asset_dir_ = std::filesystem::path(note_dir_) / "assets";
    }
}

void MarkdownPreview::set_style_path(const std::string& css_path) {
    css_path_ = css_path;
}

void MarkdownPreview::set_dark(bool dark) {
    dark_ = dark;
    palette_ = dark ? dark_palette() : light_palette();
    style_ = default_style(palette_);
    dirty_ = true;
    if (canvas_) canvas_->queue_draw();
}

void MarkdownPreview::render(const std::string& markdown) {
    source_ = markdown;
    dirty_ = true;
    if (canvas_) canvas_->queue_draw();
}

void MarkdownPreview::set_scroll_fraction(double fraction) {
    auto adj = get_vadjustment();
    if (!adj) return;
    double upper = adj->get_upper() - adj->get_page_size();
    if (upper <= 0.0) return;
    adj->set_value(fraction * upper);
}

void MarkdownPreview::relayout_if_needed() {
    int w = 600;
    if (canvas_) {
        auto allocation = canvas_->get_allocation();
        w = allocation.get_width();
        if (w <= 0) w = 600;
    }
    if (!dirty_ && !source_.empty() && w == last_layout_width_) return;
    last_layout_width_ = w;

    remin::markdown::MarkdownAst ast = remin::markdown::MarkdownAst::parse(source_);

    style_ = default_style(palette_);
    if (!css_path_.empty()) {
        std::ifstream ifs(css_path_);
        if (ifs) {
            std::stringstream ss;
            ss << ifs.rdbuf();
            style_ = apply_style(ss.str(), style_, palette_);
        }
    }

    auto resolve_asset = [&](const std::string& ref) -> std::optional<std::filesystem::path> {
        return resolve_local_image(ref, resolved_asset_dir_, std::filesystem::path(note_dir_));
    };

    const double content_w = std::max(1.0, static_cast<double>(w) - 2.0 * kPreviewMargin);

    layout_ = layout_document(ast, style_, content_w, resolve_asset, {}, false);

    // Make the canvas report the full content height so the ScrolledWindow's
    // adjustment has a scrollable range (a DrawingArea has zero natural size,
    // which would otherwise make the preview unscrollable).
    double h = 0.0;
    for (const auto& b : layout_.blocks)
        if (b.y + b.height > h) h = b.y + b.height;
    if (canvas_) canvas_->set_size_request(-1, static_cast<int>(h) + static_cast<int>(kPreviewMargin));
    dirty_ = false;
}

void MarkdownPreview::on_canvas_draw(const Cairo::RefPtr<Cairo::Context>& cr, int /*w*/, int /*h*/) {
    relayout_if_needed();

    // Clear background
    Gdk::RGBA bg = dark_ ? Gdk::RGBA("#1e1e2e") : Gdk::RGBA("#ffffff");
    Gdk::Cairo::set_source_rgba(cr, bg);
    cr->paint();

    // Draw all blocks: the canvas is allocated at full content height and GTK
    // clips the drawing to the damaged/visible region.
    if (!layout_.blocks.empty()) {
        cr->save();
        cr->translate(kPreviewMargin, kPreviewMargin);
        draw_blocks_range(cr, layout_.blocks, 0, layout_.blocks.size(), style_);
        cr->restore();
    }
}

std::size_t MarkdownPreview::first_visible_block(double vis_top) const {
    for (std::size_t i = 0; i < layout_.blocks.size(); ++i)
        if (layout_.blocks[i].y + layout_.blocks[i].height >= vis_top)
            return i;
    return 0;
}

std::size_t MarkdownPreview::last_visible_block(double vis_bottom, std::size_t first) const {
    for (std::size_t i = layout_.blocks.size(); i-- > first; )
        if (layout_.blocks[i].y <= vis_bottom)
            return i;
    return layout_.blocks.empty() ? 0 : layout_.blocks.size() - 1;
}

} // namespace remin::gui