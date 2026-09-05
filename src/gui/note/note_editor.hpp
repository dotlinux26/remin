#pragma once

#include <gtkmm.h>
#include <gtksourceview/gtksource.h>
#include <functional>
#include <string>

namespace remin::gui {

// A note editor tab (edit surface only) using GtkSourceView 5 C API.
//
// Features:
// - Built-in line numbers (synchronized with word wrap)
// - Current line highlighting
// - Mark attributes for bookmarks/breakpoints/errors
// - Word wrap (GTK_WRAP_WORD_CHAR)
// - Monospace font
// - Gutter for marks (bookmarks, breakpoints, errors)
class NoteEditor : public Gtk::Box {
public:
    // on_change is called after every buffer edit (activity signal).
    explicit NoteEditor(std::function<void()> on_change = {});

    [[nodiscard]] std::string text() const;
    void set_text(const std::string& text);

    void focus_editor();

    // Switch the GtkSourceView color scheme to match the app theme.
    void set_theme(bool dark);

    // Show the find/replace bar and focus its entry (Ctrl+F / Ctrl+H).
    void show_find(bool show_replace = false);

    // Clear find/replace entry texts.
    void clear_find_replace_entries();

    // Ctrl+S explicit-save hook (the session flushes the autosaver).
    void set_on_save(std::function<void()> on_save) { on_save_ = std::move(on_save); }
    // Live preview hook, debounced ~200 ms, with the current text.
    void set_on_preview(std::function<void(const std::string&)> cb) {
        on_preview_ = std::move(cb);
    }

    void request_save() { if (on_save_) on_save_(); }

    // Public find/replace actions — operate on the active search context.
    // The search text is taken from the MainWindow find bar via set_search_text().
    void set_search_text(const Glib::ustring& text);
    void set_replace_text(const Glib::ustring& text);
    void search_next();
    void search_previous();
    void do_replace();
    void do_replace_all();
    [[nodiscard]] int match_count() const;

    // Dirty tracking — mirrors GtkSourceBuffer::modified.
    [[nodiscard]] bool is_modified() const;
    void set_modified(bool m);

    // Access to the underlying GtkSourceBuffer for direct signal connections (C API).
    GtkSourceBuffer* source_buffer() const;

    // Access to the underlying Gtk::TextBuffer (C++ API) for signal connections.
    // GtkSourceBuffer inherits from GtkTextBuffer, so this works for signals.
    [[nodiscard]] Glib::RefPtr<Gtk::TextBuffer> buffer() const;

    // Vertical scroll adjustment of the editor (for editor ↔ preview sync).
    [[nodiscard]] Glib::RefPtr<Gtk::Adjustment> vadjustment() const;

private:
    void on_buffer_changed();
    bool on_preview_tick();
    void do_search(bool forward);

    std::function<void()> on_change_;
    std::function<void()> on_save_;
    std::function<void(const std::string&)> on_preview_;

    // Replacement string for do_replace()/do_replace_all().
    Glib::ustring replace_text_;

    Gtk::ScrolledWindow* scroller_{nullptr};
    GtkSourceView* source_view_{nullptr};
    GtkSourceBuffer* source_buffer_{nullptr};
    GtkSourceSearchContext* search_context_{nullptr};

    sigc::connection preview_timer_;
    bool preview_pending_{false};
    int line_spacing_{2}; // pixels above AND below each line
};

} // namespace remin::gui
