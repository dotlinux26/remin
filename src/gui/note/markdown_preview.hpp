#pragma once

#include <gtkmm.h>
#include <string>
#include <vector>
#include <optional>
#include <memory>

#include "gui/markdown/markdown_ast.hpp"
#include "gui/markdown/markdown_layout.hpp"
#include "gui/markdown/markdown_style.hpp"

namespace remin::gui {

class MarkdownPreview : public Gtk::ScrolledWindow {
public:
    MarkdownPreview();
    ~MarkdownPreview() override;

    void render(const std::string& markdown);

    // Set the vertical scroll position as a fraction (0.0..1.0) of the content
    // height — used for editor ↔ preview scroll synchronisation.
    void set_scroll_fraction(double fraction);

    // Vertical scroll adjustment of the preview pane.
    [[nodiscard]] Glib::RefPtr<Gtk::Adjustment> vadjustment() {
        return get_vadjustment();
    }

    // Local asset resolution roots. `asset_dir` defaults to note_dir/assets.
    void set_note_dir(const std::string& note_dir, const std::string& asset_dir = {});

    // User CSS path (settings). Empty = builtin stylesheet.
    void set_style_path(const std::string& css_path);

    // Follow the active dark/light theme.
    void set_dark(bool dark);

private:
    void relayout_if_needed();
    void on_canvas_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    std::size_t first_visible_block(double vis_top) const;
    std::size_t last_visible_block(double vis_bottom, std::size_t first) const;

    Gtk::DrawingArea* canvas_{nullptr};
    std::string source_;
    remin::markdown::MarkdownAst ast_;
    remin::markdown::LayoutResult layout_;
    remin::markdown::StyleSheet style_;
    remin::markdown::ReminPalette palette_{remin::markdown::light_palette()};
    bool dark_{false};
    bool dirty_{false};
    int last_layout_width_{0};

    std::string note_dir_;
    std::string asset_dir_;
    std::string css_path_;
    std::filesystem::path resolved_asset_dir_;
    bool cursor_over_link_{false};

    // Cache for resolved asset pixbufs
    std::vector<std::pair<std::string, Gdk::Pixbuf>> resolve_asset_cache_;
};

} // namespace remin::gui