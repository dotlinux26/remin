#include "gui/note/note_editor.hpp"

#include <algorithm>
#include <cmath>

namespace remin::gui {

NoteEditor::NoteEditor(std::function<void()> on_change)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), on_change_(std::move(on_change)) {
    set_hexpand(true);
    set_vexpand(true);

    // GtkSourceView with built-in line numbers, current-line highlight, word wrap.
    source_view_ = GTK_SOURCE_VIEW(gtk_source_view_new());
    source_buffer_ = gtk_source_buffer_new(nullptr);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(source_view_), GTK_TEXT_BUFFER(source_buffer_));
    g_object_unref(source_buffer_); // the view now owns the buffer reference

    gtk_source_view_set_show_line_numbers(source_view_, TRUE);
    gtk_source_view_set_highlight_current_line(source_view_, TRUE);

    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(source_view_), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(source_view_), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(source_view_), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(source_view_), 8);

    // Change tracking (activity signal -> autosaver + live preview debounce).
    source_buffer_ = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(source_view_)));
    g_signal_connect(source_buffer_, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer self) {
        static_cast<NoteEditor*>(self)->on_buffer_changed();
    }), this);

    // Search context used by the shared MainWindow find bar.
    search_context_ = gtk_source_search_context_new(source_buffer_, nullptr);

    scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller_->set_hexpand(true);
    scroller_->set_vexpand(true);
    scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroller_->set_child(*Glib::wrap(GTK_WIDGET(source_view_)));
    append(*scroller_);
}

std::string NoteEditor::text() const {
    GtkTextIter start, end;
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char* raw = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    std::string result(raw ? raw : "");
    g_free(raw);
    return result;
}

void NoteEditor::set_text(const std::string& content) {
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(source_buffer_),
                             content.c_str(), static_cast<int>(content.length()));
    preview_pending_ = false;
}

void NoteEditor::on_buffer_changed() {
    if (on_change_) on_change_();
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

// The find/replace UI lives in MainWindow's shared find bar; this hook is kept
// for API compatibility (NoteTabView still calls it).
void NoteEditor::show_find(bool) {}

void NoteEditor::clear_find_replace_entries() {}

void NoteEditor::set_search_text(const Glib::ustring& text) {
    if (!search_context_) return;
    auto* settings = gtk_source_search_context_get_settings(search_context_);
    gtk_source_search_settings_set_search_text(settings, text.c_str());
    gtk_source_search_context_set_highlight(search_context_, !text.empty());
}

void NoteEditor::set_replace_text(const Glib::ustring& text) {
    replace_text_ = text;
}

int NoteEditor::match_count() const {
    if (!search_context_) return 0;
    return gtk_source_search_context_get_occurrences_count(search_context_);
}

void NoteEditor::do_search(bool forward) {
    if (!search_context_) return;
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    GtkTextIter iter, match_start, match_end;
    gboolean has_wrapped = FALSE;

    auto* settings = gtk_source_search_context_get_settings(search_context_);
    const char* needle = gtk_source_search_settings_get_search_text(settings);
    if (!needle || !*needle) return;

    // Start from the far side of the current selection so repeated Enter jumps.
    GtkTextIter sel_start, sel_end;
    if (gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end)) {
        iter = forward ? sel_end : sel_start;
    } else {
        gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                                         gtk_text_buffer_get_insert(buffer));
    }

    gboolean found = forward
        ? gtk_source_search_context_forward(search_context_, &iter, &match_start, &match_end, &has_wrapped)
        : gtk_source_search_context_backward(search_context_, &iter, &match_start, &match_end, &has_wrapped);

    if (found) {
        gtk_text_buffer_select_range(buffer, &match_start, &match_end);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(source_view_),
                                     gtk_text_buffer_get_insert(buffer),
                                     0.1, TRUE, 0.0, 0.0);
    }
}

void NoteEditor::search_next() { do_search(true); }
void NoteEditor::search_previous() { do_search(false); }

void NoteEditor::do_replace() {
    if (!search_context_ || replace_text_.empty()) return;
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);

    GtkTextIter start, end;
    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) return;
    gtk_text_iter_order(&start, &end);

    GError* error = nullptr;
    if (gtk_source_search_context_replace(search_context_, &start, &end,
                                          replace_text_.c_str(), -1, &error)) {
        search_next();
    }
    if (error) g_error_free(error);
}

void NoteEditor::do_replace_all() {
    if (!search_context_ || replace_text_.empty()) return;

    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    GError* error = nullptr;
    guint count = gtk_source_search_context_replace_all(
        search_context_, replace_text_.c_str(), -1, &error);
    if (error) {
        g_error_free(error);
        return;
    }
    if (count > 0) {
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(source_view_),
                                     gtk_text_buffer_get_insert(buffer),
                                     0.1, TRUE, 0.0, 0.0);
    }
}

bool NoteEditor::is_modified() const {
    return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(source_buffer_));
}

void NoteEditor::set_modified(bool modified) {
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(source_buffer_), modified);
}

GtkSourceBuffer* NoteEditor::source_buffer() const {
    return source_buffer_;
}

Glib::RefPtr<Gtk::TextBuffer> NoteEditor::buffer() const {
    return Glib::wrap(GTK_TEXT_BUFFER(source_buffer_));
}

Glib::RefPtr<Gtk::Adjustment> NoteEditor::vadjustment() const {
    return scroller_ ? scroller_->get_vadjustment() : Glib::RefPtr<Gtk::Adjustment>();
}

void NoteEditor::focus_editor() {
    gtk_widget_grab_focus(GTK_WIDGET(source_view_));
}

} // namespace remin::gui