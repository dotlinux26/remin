#include "gui/note/note_editor.hpp"

#include <adwaita.h>
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

    // Search context used by the shared MainWindow find bar. Enable
    // occurrence highlighting and tint every match orange so results are easy
    // to spot in both light and dark themes (the stock gray match is too faint).
    search_context_ = gtk_source_search_context_new(source_buffer_, nullptr);
    // Search semantics (case sensitivity, whole-word, regex, wrap-around,
    // occurrence counting, replace/replace_all) are delegated to
    // GtkSourceSearchContext.  The visual highlight layer is Remin's own (two
    // GtkTextTags in refresh_match_highlight), because the stock match-style
    // rendering on the supported GtkSourceView stack does not produce the
    // requested contrast.  We disable the library's internal highlight tag so
    // it never competes with our tags.
    gtk_source_search_context_set_highlight(search_context_, FALSE);
    // Search wraps around at document edges so Next/Previous keep cycling.
    auto* search_settings = gtk_source_search_context_get_settings(search_context_);
    gtk_source_search_settings_set_wrap_around(search_settings, TRUE);

    // Theme-aware color scheme (GtkSourceView does not follow the GTK theme
    // by default, so the editor would stay light in dark mode).
    set_theme(adw_style_manager_get_dark(adw_style_manager_get_default()));
    g_signal_connect(adw_style_manager_get_default(), "notify::color-scheme",
                     G_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
                         static_cast<NoteEditor*>(self)->set_theme(
                             adw_style_manager_get_dark(
                                 adw_style_manager_get_default()));
                     }),
                     this);

    scroller_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller_->set_hexpand(true);
    scroller_->set_vexpand(true);
    scroller_->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroller_->set_child(*Glib::wrap(GTK_WIDGET(source_view_)));
    append(*scroller_);

    // Repaint only the visible search highlights when the view scrolls.
    if (auto vadj = scroller_->get_vadjustment()) {
        vadj->signal_value_changed().connect(
            sigc::mem_fun(*this, &NoteEditor::on_scroll_changed));
    }
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
    // Re-paint search highlights when the text changes so they track edits,
    // debounced so a large document isn't rescanned per keystroke.
    if (search_context_) {
        // Edits shift match offsets; the previously "current" occurrence is
        // no longer valid until the user navigates again.
        current_occurrence_ = -1;
        current_match_start_ = -1;
        current_match_end_ = -1;
        highlight_pending_ = true;
        if (!highlight_timer_.connected()) {
            highlight_timer_ = Glib::signal_timeout().connect(
                sigc::mem_fun(*this, &NoteEditor::on_highlight_tick), 120);
        }
    }
    preview_pending_ = true;
    if (!preview_timer_.connected()) {
        preview_timer_ = Glib::signal_timeout().connect(
            sigc::mem_fun(*this, &NoteEditor::on_preview_tick), 200);
    }
}

bool NoteEditor::on_highlight_tick() {
    if (highlight_pending_) refresh_match_highlight();
    highlight_pending_ = false;
    return false;
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

void NoteEditor::set_theme(bool dark) {
    auto* mgr = gtk_source_style_scheme_manager_get_default();
    const char* id = dark ? "Adwaita-dark" : "Adwaita";
    GtkSourceStyleScheme* scheme = gtk_source_style_scheme_manager_get_scheme(mgr, id);
    if (!scheme) {
        id = dark ? "oblivion" : "classic";
        scheme = gtk_source_style_scheme_manager_get_scheme(mgr, id);
    }
    if (scheme) gtk_source_buffer_set_style_scheme(source_buffer_, scheme);
    refresh_search_colors();
}

void NoteEditor::set_search_text(const Glib::ustring& text) {
    if (!search_context_) return;
    auto* settings = gtk_source_search_context_get_settings(search_context_);
    gtk_source_search_settings_set_search_text(settings, text.c_str());
    // A new term invalidates the old current-match state.
    current_occurrence_ = -1;
    current_match_start_ = -1;
    current_match_end_ = -1;
    refresh_match_highlight();
}

void NoteEditor::set_search_tag_priorities() {
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    if (!buffer || !search_match_tag_ || !search_current_tag_) return;
    auto* table = gtk_text_buffer_get_tag_table(buffer);
    const int size = gtk_text_tag_table_get_size(table);
    // GtkTextTag priority must be < table size (it is an index into the
    // table); the maximum legal value is size-1.  We give the current-match
    // tag the highest priority and the other-match tag one below it so Remin's
    // highlight renders above any syntax-highlighting tags in the table.
    const int hi = size - 1;
    const int lo = size > 1 ? size - 2 : 0;
    gtk_text_tag_set_priority(search_match_tag_, lo);
    gtk_text_tag_set_priority(search_current_tag_, hi);
}

void NoteEditor::ensure_search_tags() {
    if (search_match_tag_ && search_current_tag_) return;
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    if (!buffer) return;
    if (!search_match_tag_)
        search_match_tag_ = gtk_text_buffer_create_tag(buffer,
            "remin-search-match", nullptr);
    if (!search_current_tag_)
        search_current_tag_ = gtk_text_buffer_create_tag(buffer,
            "remin-search-current", nullptr);
    set_search_tag_priorities();
}

void NoteEditor::refresh_search_colors() {
    ensure_search_tags();
    set_search_tag_priorities();
    auto* ctx = gtk_widget_get_style_context(GTK_WIDGET(source_view_));
    GdkRGBA c;
    if (gtk_style_context_lookup_color(ctx, "search_match", &c))
        g_object_set(search_match_tag_, "background-rgba", &c, nullptr);
    if (gtk_style_context_lookup_color(ctx, "search_current", &c))
        g_object_set(search_current_tag_, "background-rgba", &c, nullptr);
    if (gtk_style_context_lookup_color(ctx, "search_current_fg", &c))
        g_object_set(search_current_tag_, "foreground-rgba", &c, nullptr);
    g_object_set(search_current_tag_, "weight", 700, nullptr);
}

// ---- Viewport-aware search highlight --------------------------------------

void NoteEditor::visible_offset_window(int& start, int& end) {
    start = 0;
    end = -1;
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    if (!buffer) return;

    GtkTextIter buf_start, buf_end;
    gtk_text_buffer_get_bounds(buffer, &buf_start, &buf_end);
    const int buf_len = gtk_text_iter_get_offset(&buf_end);

    GdkRectangle visible;
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(source_view_), &visible);

    GtkTextIter top, bottom;
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(source_view_), &top, 0, visible.y);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(source_view_), &bottom, 0, visible.y + visible.height);

    // If the view isn't laid out yet (zero-height visible rect, e.g. the tab
    // hasn't been mapped), fall back to covering the whole document so that a
    // fresh search still paints highlights.
    if (visible.height <= 0 || visible.width <= 0) {
        start = 0;
        end = buf_len;
        return;
    }

    // Overscan: walk up/down an extra N lines so highlights don't flicker when
    // scrolling small amounts.
    for (int i = 0; i < kOverscanLines; ++i) {
        if (!gtk_text_iter_backward_line(&top)) break;
    }
    for (int i = 0; i < kOverscanLines; ++i) {
        if (!gtk_text_iter_forward_line(&bottom)) break;
    }

    start = gtk_text_iter_get_offset(&top);
    end = gtk_text_iter_get_offset(&bottom);
    if (end > buf_len) end = buf_len;
}

void NoteEditor::remove_all_search_highlights() {
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    if (!buffer) return;
    GtkTextIter b, e;
    gtk_text_buffer_get_bounds(buffer, &b, &e);
    if (search_match_tag_)
        gtk_text_buffer_remove_tag(buffer, search_match_tag_, &b, &e);
    if (search_current_tag_)
        gtk_text_buffer_remove_tag(buffer, search_current_tag_, &b, &e);
}

void NoteEditor::render_search_highlights() {
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    if (!buffer || !search_context_) return;
    ensure_search_tags();

    // Remove stale highlights from the whole doc first (cheap enough, and
    // guarantees we never leave tags behind in regions we no longer cover).
    remove_all_search_highlights();

    const char* needle = gtk_source_search_settings_get_search_text(
        gtk_source_search_context_get_settings(search_context_));
    if (!needle || !*needle) return;

    int win_start, win_end;
    visible_offset_window(win_start, win_end);
    if (win_end < 0) return;

    GtkTextIter probe;
    gtk_text_buffer_get_iter_at_offset(buffer, &probe, win_start);
    GtkTextIter m_start, m_end;
    gboolean wrapped = FALSE;

    // Enumerate occurrences but only tag the ones falling inside the visible
    // window.  GtkSourceSearchContext caches occurrence data, so navigation
    // and this local enumeration stay O(visible range), not O(whole doc).
    //
    // IMPORTANT: wrap-around is enabled on the search settings, which means
    // forward() will KEEP returning matches after reaching the end of the
    // buffer by wrapping around — forever.  We must break as soon as the
    // context reports a wrap, otherwise this loop never terminates.  We also
    // stop when the match start leaves our window.
    while (gtk_source_search_context_forward(search_context_, &probe,
                                             &m_start, &m_end, &wrapped)) {
        if (wrapped) break;        // cycled past end of buffer — stop
        int ms = gtk_text_iter_get_offset(&m_start);
        int me = gtk_text_iter_get_offset(&m_end);
        if (ms > win_end) break;   // beyond our window — stop
        if (me <= win_start) {     // before our window — skip
            probe = m_end;
            continue;
        }
        gtk_text_buffer_apply_tag(buffer, search_match_tag_, &m_start, &m_end);
        probe = m_end;

        // Zero-width matches (e.g. regex `a*`) don't advance probe; force a
        // step so we can't spin forever on empty matches.
        if (gtk_text_iter_get_offset(&probe) == ms)
            gtk_text_iter_forward_char(&probe);
    }

    // Re-apply the current-match tag (higher priority than match) if it lies
    // within our window.  Its offsets were captured at search time, so we
    // don't need to re-scan to locate it.
    if (current_match_start_ >= 0 &&
        current_match_start_ <= win_end && current_match_end_ >= win_start) {
        GtkTextIter cs, ce;
        gtk_text_buffer_get_iter_at_offset(buffer, &cs, current_match_start_);
        gtk_text_buffer_get_iter_at_offset(buffer, &ce, current_match_end_);
        gtk_text_buffer_apply_tag(buffer, search_current_tag_, &cs, &ce);
    }
}

void NoteEditor::schedule_viewport_render() {
    if (scroll_pending_) return;
    scroll_pending_ = true;
    scroll_timer_.disconnect();
    scroll_timer_ = Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &NoteEditor::on_scroll_tick), 16);
}

bool NoteEditor::on_scroll_tick() {
    scroll_pending_ = false;
    render_search_highlights();
    return false;
}

void NoteEditor::on_scroll_changed() {
    schedule_viewport_render();
}

// Entry point: called on search-text change / document edit (debounced).
void NoteEditor::refresh_match_highlight() {
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    if (!buffer || !search_context_) return;
    ensure_search_tags();
    refresh_search_colors();
    render_search_highlights();
}

void NoteEditor::update_current_from_selection() {
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    if (!buffer || !search_context_) return;
    GtkTextIter sel_start, sel_end;
    if (!gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end)) return;

    // The current match's buffer offsets are taken directly from the live
    // selection — this is what Replace must act on and it must be set on every
    // navigation call, regardless of the async occurrence scan state.
    current_match_start_ = gtk_text_iter_get_offset(&sel_start);
    current_match_end_ = gtk_text_iter_get_offset(&sel_end);

    // Occurrence index is queried from the search context's cached scan. It
    // returns a 1-based "position" (0 = interval is not an occurrence, -1 =
    // region not scanned yet), so store a 0-based index. When the async scan
    // has not finished (-1/0) we keep the previous index rather than clobber
    // it; current_search_position() falls back to a local count in that case.
    int pos = gtk_source_search_context_get_occurrence_position(
        search_context_, &sel_start, &sel_end);
    if (pos > 0)
        current_occurrence_ = pos - 1;
}

void NoteEditor::set_replace_text(const Glib::ustring& text) {
    replace_text_ = text;
}

int NoteEditor::match_count() const {
    if (!search_context_) return 0;
    return gtk_source_search_context_get_occurrences_count(search_context_);
}

std::pair<int,int> NoteEditor::current_search_position() const {
    if (!search_context_) return {0, 0};
    const int total = match_count();
    if (total <= 0) return {0, 0};

    // Cheap path: use the index cached from the occurrence scan when it is
    // valid. It may be stale while the scan is still running.
    if (current_occurrence_ >= 0)
        return {current_occurrence_ + 1, total};

    // Fallback: the async occurrence scan had not reached our match when we
    // last navigated, so derive the position by counting preceding matches
    // locally. This is O(distance to the match), which is fine for the rare
    // case where the cached scan is not yet ready.
    if (current_match_start_ < 0) return {0, total};
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);
    GtkTextIter probe;
    gtk_text_buffer_get_start_iter(buffer, &probe);
    const char* needle = gtk_source_search_settings_get_search_text(
        gtk_source_search_context_get_settings(search_context_));
    if (!needle || !*needle) return {0, total};
    int count = 0;
    GtkTextIter m_start, m_end;
    gboolean wrapped = false;
    while (gtk_source_search_context_forward(search_context_, &probe,
                                             &m_start, &m_end, &wrapped)) {
        if (wrapped) break;
        int ms = gtk_text_iter_get_offset(&m_start);
        if (ms > current_match_start_) break;
        count++;
        probe = m_end;
        if (gtk_text_iter_get_offset(&probe) == ms)
            gtk_text_iter_forward_char(&probe);
    }
    return {count, total};
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
        // Record the focused occurrence from the context's cached data; no
        // full-document rescan needed.
        update_current_from_selection();
        render_search_highlights();
    } else if (current_occurrence_ < 0) {
        // First search from a fresh state: nothing before the cursor exists.
        if (!has_wrapped && forward) {
            // Search from document start so the user gets a first match even
            // when the cursor sits past the last occurrence.
            GtkTextIter start;
            gtk_text_buffer_get_start_iter(buffer, &start);
            if (gtk_source_search_context_forward(search_context_, &start,
                                                  &match_start, &match_end,
                                                  &has_wrapped)) {
                gtk_text_buffer_select_range(buffer, &match_start, &match_end);
                gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(source_view_),
                                             gtk_text_buffer_get_insert(buffer),
                                             0.1, TRUE, 0.0, 0.0);
                update_current_from_selection();
                render_search_highlights();
            }
        }
    }
}

void NoteEditor::search_next() { do_search(true); }
void NoteEditor::search_previous() { do_search(false); }

void NoteEditor::do_replace() {
    if (!search_context_ || replace_text_.empty()) return;
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(source_buffer_);

    // Resolve the exact match to replace without depending on navigation
    // state (current_match_* is only set when Find was explicitly run, e.g.
    // Enter/Next/Prev). Order of preference:
    //   a) a live selection that really delimits an occurrence;
    //   b) otherwise the next occurrence from the caret — i.e. behave as if
    //      the user pressed Enter once, then Replace. Typing a term in the
    //      find box and pressing Replace must just work.
    GtkTextIter t_start, t_end;
    bool resolved = false;

    GtkTextIter sel_start, sel_end;
    if (gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end)) {
        gtk_text_iter_order(&sel_start, &sel_end);
        // Only replace a selection the search context recognises as an
        // occurrence; arbitrary selections are left untouched.
        if (gtk_source_search_context_get_occurrence_position(
                search_context_, &sel_start, &sel_end) > 0) {
            t_start = sel_start;
            t_end = sel_end;
            resolved = true;
        }
    }

    if (!resolved) {
        GtkTextIter probe;
        if (gtk_text_buffer_get_selection_bounds(buffer, &sel_start, &sel_end)) {
            gtk_text_iter_order(&sel_start, &sel_end);
            probe = sel_start;
        } else {
            gtk_text_buffer_get_iter_at_mark(buffer, &probe,
                                             gtk_text_buffer_get_insert(buffer));
        }
        gboolean wrapped = FALSE;
        resolved = gtk_source_search_context_forward(
            search_context_, &probe, &t_start, &t_end, &wrapped);
    }

    if (!resolved) return;

    GtkTextIter m_start, m_end;
    gtk_text_buffer_get_iter_at_offset(buffer, &m_start,
                                       gtk_text_iter_get_offset(&t_start));
    gtk_text_buffer_get_iter_at_offset(buffer, &m_end,
                                       gtk_text_iter_get_offset(&t_end));
    // Sanity: never clobber text that is not exactly one occurrence. 0 means
    // the region was scanned and the interval is not an occurrence; -1 just
    // means the async scan has not reached it yet, which is fine.
    if (gtk_source_search_context_get_occurrence_position(
            search_context_, &m_start, &m_end) == 0)
        return;

    GError* error = nullptr;
    if (gtk_source_search_context_replace(search_context_, &m_start, &m_end,
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