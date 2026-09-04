#include "gui/note/note_editor.hpp"

#include <chrono>
#include <cmath>
#include <algorithm>

namespace remin::gui {

NoteEditor::NoteEditor(std::function<void()> on_change)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), on_change_(std::move(on_change)) {
    set_hexpand(true);
    set_vexpand(true);

    buffer_ = Gtk::TextBuffer::create();
    buffer_->signal_changed().connect(sigc::mem_fun(*this, &NoteEditor::on_buffer_changed));

    gutter_buffer_ = Gtk::TextBuffer::create();
    gutter_ = Gtk::make_managed<Gtk::TextView>(gutter_buffer_);
    gutter_->set_editable(false);
    gutter_->set_cursor_visible(false);
    gutter_->set_monospace(true);
    gutter_->set_left_margin(8);
    gutter_->set_right_margin(8);
    gutter_->add_css_class("remin-gutter");
    gutter_->set_vexpand(true);

    view_ = Gtk::make_managed<Gtk::TextView>(buffer_);
    view_->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    view_->set_monospace(true);
    view_->set_left_margin(8);
    view_->set_right_margin(8);
    view_->set_vexpand(true);

    // Keep the line-number gutter and the editor perfectly in sync: identical
    // font description and identical vertical spacing on both views. All row
    // padding comes from the shared line_spacing_ value, never from per-view
    // top/bottom margins (which would desync the two columns).
    apply_line_spacing();

    // External scroll sync: gutter in its own ScrolledWindow (never scrolls
    // horizontally, external vertical policy so we control it), text view in
    // its own ScrolledWindow. Sync vadjustments for line-number scroll.
    gutter_scroll_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    gutter_scroll_->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::EXTERNAL);
    gutter_scroll_->set_child(*gutter_);
    gutter_scroll_->set_vexpand(true);

    scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller_->set_hexpand(true);
    scroller_->set_vexpand(true);
    scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroller_->set_child(*view_);

    // Horizontal box: gutter | text view
    auto* hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    hbox->set_hexpand(true);
    hbox->set_vexpand(true);
    hbox->append(*gutter_scroll_);
    hbox->append(*scroller_);
    append(*hbox);

    // Sync gutter scroll with text view scroll. Mirror the full adjustment so
    // the gutter's upper/page_size always match the editor's; otherwise on a
    // fast Enter the gutter's upper can lag behind the value we want to apply
    // and GTK clamps it, leaving the line numbers stuck for a frame.
    auto text_vadj = scroller_->get_vadjustment();
    auto gutter_vadj = gutter_scroll_->get_vadjustment();
    auto sync_gutter = [text_vadj, gutter_vadj]() {
        auto tv = text_vadj;
        auto gv = gutter_vadj;
        gv->set_lower(tv->get_lower());
        gv->set_upper(tv->get_upper());
        gv->set_page_size(tv->get_page_size());
        gv->set_step_increment(tv->get_step_increment());
        gv->set_value(tv->get_value());
    };
    text_vadj->signal_value_changed().connect(sync_gutter);
    text_vadj->signal_changed().connect(sync_gutter);

    update_line_numbers();
}

void NoteEditor::apply_line_spacing() {
    auto apply = [this](Gtk::TextView& tv) {
        tv.set_pixels_above_lines(line_spacing_);
        tv.set_pixels_below_lines(line_spacing_);
    };
    apply(*gutter_);
    apply(*view_);
}

void NoteEditor::set_line_spacing(int pixels) {
    line_spacing_ = std::max(0, pixels);
    apply_line_spacing();
}

std::string NoteEditor::text() const {
    return buffer_->get_text(false);
}

void NoteEditor::set_text(const std::string& text) {
    const bool had = preview_pending_;
    buffer_->set_text(text);
    preview_pending_ = had;
    update_line_numbers();
}

void NoteEditor::show_find(bool show_replace) {
    // Find/replace is now handled by MainWindow's shared find bar.
    // This method is kept for API compatibility.
    (void)show_replace;
}

void NoteEditor::on_buffer_changed() {
    update_line_numbers();
    // Activity signal -> unified autosaver.
    if (on_change_) on_change_();

    // Debounced live markdown preview (~200 ms), not on every keystroke.
    preview_pending_ = true;
    if (!preview_timer_.connected()) {
        preview_timer_ = Glib::signal_timeout().connect(
            sigc::mem_fun(*this, &NoteEditor::on_preview_tick), 200);
    }
}

bool NoteEditor::on_preview_tick() {
    if (preview_pending_ && on_preview_) on_preview_(text());
    preview_pending_ = false;
    preview_timer_.disconnect();
    return false;
}

void NoteEditor::update_line_numbers() {
    const int count = buffer_->get_line_count();
    Glib::ustring nums;
    for (int i = 1; i <= count; ++i) {
        nums += std::to_string(i);
        nums += '\n';
    }
    gutter_buffer_->set_text(nums);
    update_gutter_width();
}

// Auto-size the native gutter window so the numbers never clip: reserve exactly
// enough horizontal space for the widest line number (monospace), plus margins.
void NoteEditor::update_gutter_width() {
    const int count = buffer_->get_line_count();
    const int digits = (count <= 0) ? 1 : static_cast<int>(std::floor(std::log10(static_cast<double>(count)))) + 1;
    // Breathing room after the last digit; the "+1" grows the gutter only as
    // the wrap count grows, so wide line numbers always fit.
    const int chars = std::max(digits, 1) + 1;

    // Measure a probe of the widest possible value in this view's monospace
    // font so the reserved width matches the real glyph width.
    auto layout = Pango::Layout::create(view_->get_pango_context());
    layout->set_text(Glib::ustring(chars, '9'));
    int pw = 0, ph = 0;
    layout->get_pixel_size(pw, ph);
    (void)ph;

    // Account for the gutter's own horizontal margins/insets.
    const int lm = gutter_->get_left_margin();
    const int rm = gutter_->get_right_margin();
    constexpr int extra = 4; // safe inner padding so glyphs never clip

    gutter_->set_size_request(pw + lm + rm + extra, -1);
}

bool NoteEditor::is_modified() const {
    return buffer_ && buffer_->get_modified();
}

void NoteEditor::set_modified(bool m) {
    if (buffer_) buffer_->set_modified(m);
}

void NoteEditor::do_replace() {
    // Find/replace is now handled by MainWindow's shared find bar.
    // This method is kept for API compatibility.
}

void NoteEditor::do_replace_all() {
    // Find/replace is now handled by MainWindow's shared find bar.
    // This method is kept for API compatibility.
}

void NoteEditor::do_find_next(bool backwards) {
    // Find/replace is now handled by MainWindow's shared find bar.
    // This method is kept for API compatibility.
    (void)backwards;
}

void NoteEditor::clear_find_replace_entries() {
    // Find/replace is now handled by MainWindow's shared find bar.
    // This method is kept for API compatibility.
}

} // namespace remin::gui