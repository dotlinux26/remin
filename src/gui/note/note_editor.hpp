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
    // Returns {current_position_1based_or_0, total_matches}.
    // (0, total) when cursor is not on a match; (0,0) when no search active.
    [[nodiscard]] std::pair<int,int> current_search_position() const;

    // Re-tint all occurrences matching the active search text. Searches are
    // enumerated via GtkSourceSearchContext (respecting its case/whole-word/
    // regex/wrap settings); Remin owns the highlight rendering with two
    // semantic tags: "other matches" and the focused "current match".
    void refresh_match_highlight();

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
    bool on_highlight_tick();

    std::function<void()> on_change_;
    std::function<void()> on_save_;
    std::function<void(const std::string&)> on_preview_;

    // Replacement string for do_replace()/do_replace_all().
    Glib::ustring replace_text_;

    Gtk::ScrolledWindow* scroller_{nullptr};
    GtkSourceView* source_view_{nullptr};
    GtkSourceBuffer* source_buffer_{nullptr};
    GtkSourceSearchContext* search_context_{nullptr};

    // Two semantic highlight tags: "other matches" and "current match".
    // Rendered by Remin; GtkSourceSearchContext owns search semantics only.
    GtkTextTag* search_match_tag_{nullptr};
    GtkTextTag* search_current_tag_{nullptr};

    // Refreshes the highlight tag colours from the current theme palette
    // (via @define-color search_match / search_current / search_current_fg).
    void refresh_search_colors();
    void ensure_search_tags();
    void set_search_tag_priorities();

    // ---- Viewport-aware search highlight -----------------------------------
    // The current search match (by occurrence index) and its byte offsets.
    // Kept WITHOUT re-scanning the whole document on navigation/scroll (the
    // occurrence data is cached by GtkSourceSearchContext).
    int current_occurrence_{-1};      // 0-based; -1 = none
    int current_match_start_{-1};     // buffer offset of current match
    int current_match_end_{-1};       // buffer offset of current match end

    // Visible (viewport + overscan) buffer offset window.
    static constexpr int kOverscanLines = 200;

    sigc::connection scroll_timer_;
    bool scroll_pending_{false};
    void on_scroll_changed();
    void schedule_viewport_render();
    bool on_scroll_tick();
    void visible_offset_window(int& start, int& end);
    void remove_all_search_highlights();
    void render_search_highlights();
    // Locate the current occurrence after a cached forward/backward search.
    void update_current_from_selection();

    sigc::connection highlight_timer_;
    bool highlight_pending_{false};

    sigc::connection preview_timer_;
    bool preview_pending_{false};
    int line_spacing_{2}; // pixels above AND below each line
};

} // namespace remin::gui
