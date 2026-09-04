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

    view_ = Gtk::make_managed<Gtk::TextView>(buffer_);
    view_->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    view_->set_monospace(true);
    view_->set_left_margin(8);
    view_->set_right_margin(8);
    view_->set_top_margin(6);
    view_->set_bottom_margin(6);

    // Place the line-number gutter in the TextView's native LEFT text window so
    // GTK reserves horizontal space between the numbers and the edit surface
    // (they never overlap) and scrolls the gutter in sync with the text.
    view_->set_gutter(Gtk::TextWindowType::LEFT, *gutter_);
    gutter_->set_visible(true); // Ensure gutter is visible

    // Explicit scroll sync: connect view's vadjustment to gutter's vadjustment
    // so line numbers scroll with content even when word-wrap is active.
    auto vadj = view_->get_vadjustment();
    auto gutter_vadj = gutter_->get_vadjustment();
    vadj->signal_value_changed().connect([this, gutter_vadj]() {
        auto vadj = view_->get_vadjustment();
        auto gvadj = gutter_->get_vadjustment();
        if (vadj && gvadj && std::abs(vadj->get_value() - gvadj->get_value()) > 0.5) {
            gvadj->set_value(vadj->get_value());
        }
    });

    scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller_->set_hexpand(true);
    scroller_->set_vexpand(true);
    scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroller_->set_child(*view_);
    append(*scroller_);
    update_line_numbers();

    // Find / replace bar (hidden by default).
    find_bar_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    find_bar_->set_margin_top(4);
    find_bar_->set_margin_start(6);
    find_bar_->set_margin_end(6);

    auto* lbl = Gtk::make_managed<Gtk::Label>("Find:");
    find_entry_ = Gtk::make_managed<Gtk::Entry>();
    find_entry_->set_hexpand(true);
    find_entry_->signal_activate().connect([this]() { do_find_next(false); });

    auto* next = Gtk::make_managed<Gtk::Button>("Next");
    next->signal_clicked().connect([this]() { do_find_next(false); });
    auto* prev = Gtk::make_managed<Gtk::Button>("Previous");
    prev->signal_clicked().connect([this]() { do_find_next(true); });

    auto* rlbl = Gtk::make_managed<Gtk::Label>("Replace:");
    replace_entry_ = Gtk::make_managed<Gtk::Entry>();
    replace_entry_->set_hexpand(true);
    replace_btn_ = Gtk::make_managed<Gtk::Button>("Replace");
    replace_btn_->signal_clicked().connect(sigc::mem_fun(*this, &NoteEditor::do_replace));
    replace_all_btn_ = Gtk::make_managed<Gtk::Button>("Replace all");
    replace_all_btn_->signal_clicked().connect(sigc::mem_fun(*this, &NoteEditor::do_replace_all));
    auto* close = Gtk::make_managed<Gtk::Button>("✕");
    close->signal_clicked().connect([this]() { find_bar_->set_visible(false); });

    find_bar_->append(*lbl);
    find_bar_->append(*find_entry_);
    find_bar_->append(*prev);
    find_bar_->append(*next);
    find_bar_->append(*rlbl);
    find_bar_->append(*replace_entry_);
    find_bar_->append(*replace_btn_);
    find_bar_->append(*replace_all_btn_);
    find_bar_->append(*close);
    find_bar_->set_visible(false);
    append(*find_bar_);
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
    find_bar_->set_visible(true);
    replace_entry_->set_visible(show_replace);
    replace_btn_->set_visible(show_replace);
    replace_all_btn_->set_visible(show_replace);
    find_entry_->grab_focus();
}

void NoteEditor::clear_find_replace_entries() {
    find_entry_->set_text("");
    replace_entry_->set_text("");
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
    const int width = pw + lm + rm + extra;
    if (gutter_->property_width_request().get_value() != width) {
        gutter_->set_size_request(width, -1);
    }
}

void NoteEditor::do_find_next(bool backwards) {
    const Glib::ustring needle = find_entry_->get_text();
    if (needle.empty()) return;
    auto start = buffer_->get_iter_at_mark(buffer_->get_insert());
    Gtk::TextIter match_start, match_end;
    bool found = false;
    if (backwards) {
        found = start.backward_search(needle, Gtk::TextSearchFlags::VISIBLE_ONLY, match_start, match_end, buffer_->begin());
    } else {
        found = start.forward_search(needle, Gtk::TextSearchFlags::VISIBLE_ONLY, match_start, match_end, buffer_->end());
    }
    if (found) {
        buffer_->select_range(match_start, match_end);
        buffer_->place_cursor(match_end);
        view_->scroll_to(match_start, 0.3);
    }
}

void NoteEditor::do_replace() {
    const Glib::ustring needle = find_entry_->get_text();
    if (needle.empty() || !buffer_->get_has_selection()) return;
    buffer_->insert_at_cursor(replace_entry_->get_text());
    do_find_next(false);
}

void NoteEditor::do_replace_all() {
    const Glib::ustring needle = find_entry_->get_text();
    if (needle.empty()) return;
    Glib::ustring text = buffer_->get_text(false);
    const Glib::ustring repl = replace_entry_->get_text();
    int replaced = 0;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != Glib::ustring::npos) {
        text.replace(pos, needle.size(), repl);
        pos += repl.size();
        ++replaced;
    }
    if (replaced > 0) {
        buffer_->set_text(text);
    }
}

} // namespace remin::gui
