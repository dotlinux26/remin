#include "gui/note/note_editor.hpp"

#include <chrono>
#include <cmath>
#include <algorithm>
#include <memory>

namespace remin::gui {

NoteEditor::NoteEditor(std::function<void()> on_change)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), on_change_(std::move(on_change)) {
    set_hexpand(true);
    set_vexpand(true);

    // Create GtkSourceView with built-in features
    // Using GtkSourceView 5 via C API
    source_view_ = GTK_SOURCE_VIEW(gtk_source_view_new());
    source_buffer_ = gtk_source_buffer_new(nullptr);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(source_view_), GTK_TEXT_BUFFER(source_buffer_));
    g_object_unref(source_buffer_); // view takes ownership

    // Enable word wrap (GTK_WRAP_WORD_CHAR equivalent)
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(source_view_), GTK_WRAP_WORD_CHAR);

    // Enable built-in line numbers
    gtk_source_view_set_show_line_numbers(source_view_, TRUE);

    // Highlight current line
    gtk_source_view_set_highlight_current_line(source_view_, TRUE);

    // Monospace font
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(source_view_), TRUE);

    // Margins
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(source_view_), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(source_view_), 8);

    // Get the source buffer for change tracking
    source_buffer_ = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(source_view_)));
    g_signal_connect(source_buffer_, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer self) {
        static_cast<NoteEditor*>(self)->on_buffer_changed();
    }), this);

    // Scrolled window for the editor
    scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller_->set_hexpand(true);
    scroller_->set_vexpand(true);
    scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroller_->set_child(*Glib::wrap(GTK_WIDGET(source_view_)));

    append(*scroller_);

    // Debounced live markdown preview (~200 ms)
    preview_timer_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &NoteEditor::on_preview_tick), 200);
}

std::string NoteEditor::text() const {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(source_buffer_), &start, &end);
    char* text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(source_buffer_), &start, &end, FALSE);
    std::string result(text);
    g_free(text);
    return result;
}

void NoteEditor::set_text(const std::string& text) {
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(source_buffer_), text.c_str(), static_cast<int>(text.length()));
    preview_pending_ = false;
}

void NoteEditor::focus_editor() {
    gtk_widget_grab_focus(GTK_WIDGET(source_view_));
}

void NoteEditor::show_find(bool show_replace) {
    // Find/replace is now handled by MainWindow's shared find bar.
    // This method is kept for API compatibility.
    (void)show_replace;
}

void NoteEditor::clear_find_replace_entries() {
    // Find/replace is now handled by MainWindow's shared find bar.
    // This method is kept for API compatibility.
}

void NoteEditor::do_replace() {
    // Find/replace is now handled by MainWindow's shared find bar.
    // This method is kept for API compatibility.
}

void NoteEditor::do_replace_all() {
    // Find/replace is now handled by MainWindow's shared find bar.
    // This method is kept for API compatibility.
}

bool NoteEditor::is_modified() const {
    return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(source_buffer_));
}

void NoteEditor::set_modified(bool m) {
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(source_buffer_), m);
}

GtkSourceBuffer* NoteEditor::source_buffer() const {
    return source_buffer_;
}

Glib::RefPtr<Gtk::TextBuffer> NoteEditor::buffer() const {
    if (!source_buffer_) return Glib::RefPtr<Gtk::TextBuffer>();
    auto obj = Glib::wrap(G_OBJECT(source_buffer_));
    return std::dynamic_pointer_cast<Gtk::TextBuffer>(obj);
}

Glib::RefPtr<Gtk::Adjustment> NoteEditor::vadjustment() const {
    return scroller_ ? scroller_->get_vadjustment() : Glib::RefPtr<Gtk::Adjustment>();
}

void NoteEditor::on_buffer_changed() {
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
    return false;
}

} // namespace remin::gui
