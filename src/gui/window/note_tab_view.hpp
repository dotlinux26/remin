#pragma once

#include "core/id.hpp"
#include "gui/note/markdown_preview.hpp"
#include "gui/note/note_editor.hpp"
#include "gui/window/tab_view.hpp"

#include <filesystem>
#include <functional>
#include <gtkmm.h>
#include <memory>
#include <string>

namespace remin::gui {

class SessionController;

// A note tab: a NoteEditor plus an optional live MarkdownPreview shown when the
// tab is split. Both are presentations of the same note body; the editor feeds
// the preview (debounced). Persistence is routed through the unified autosaver
// / SessionController, never written by the editor directly.
class NoteTabView : public TabView {
public:
    NoteTabView(SessionController* controller, const std::string& noteId);
    ~NoteTabView() override;

    TabKind kind() const override { return TabKind::Note; }
    const std::string& title() const override { return title_; }
    const std::string& id() const override { return note_id_; }
    void activate() override;
    void deactivate() override;
    bool focus_search() override;
    void clear_search() override;

    // Toggle the markdown preview split (on by default on first split request).
    void toggle_preview();

    [[nodiscard]] std::string text() const { return editor_->text(); }

    // Explicit save now (routes to the session autosaver's flush_now, and — if
    // autosave-to-temp is on and the note has no assigned path — mirrors the
    // body to the note's temp file so nothing is lost).
    void save_now();

    // Save As: assign a file path via a chooser and write the body there. When
    // `on_done` is non-empty it is invoked after a successful save (used by the
    // close-tab "Keep" flow); on cancel nothing happens and `on_done` is not
    // called.
    void save_as(std::function<void()> on_done = {});

    // Set the note's title shown on its tab / window.
    void set_title(const std::string& title) override { title_ = title; }

    // Show find/replace bar, optionally with replace UI visible. Clears entries.
    void show_find_replace(bool show_replace);

    // Access to the editor for find/replace operations
    NoteEditor* editor() const { return editor_; }

    // Dirty tracking — true when buffer has unsaved edits.
    [[nodiscard]] bool is_modified() const;

    // True when this note is bound to a real file path (opened from the tree /
    // saved via Save As). A note with no path is a TEMP note (unsaved draft).
    [[nodiscard]] bool has_path() const;

    // The on-disk path this note is bound to ("" if it is a temp draft).
    [[nodiscard]] std::string path() const;

    // Open-file exception: when the user explicitly re-opens the file this note
    // is bound to (from the directory tree) while the note has unsaved edits, we
    // must NOT auto-reload (even if auto-reload is enabled) because that would
    // silently discard the user's work. Instead we ask: Save / Discard&Reload /
    // Cancel.
    void prompt_open_conflict();

    // Fired whenever the save state (modified flag) changes, so the tab bar can
    // refresh its unsaved-dot indicator (audit issue: update_tab_bar must run
    // on Ctrl+S / Save / Save As, not just tab switches).
    void set_save_state_callback(std::function<void()> cb) { on_save_state_ = std::move(cb); }

    // Fired after the note body is actually written to its on-disk file (Ctrl+S
    // on a path-note, or Save As). Hosts refresh the directory tree here — NOT
    // in the save-state callback (which fires on every keystroke for the dot).
    // The saved file path lets the host reconcile only the affected directory.
    void set_file_saved_callback(std::function<void(const std::filesystem::path&)> cb) { on_file_saved_ = std::move(cb); }

    // Load a file into this note tab
    void load_file(const std::filesystem::path& path);

private:
    void connect_editor();
    void set_content(Gtk::Widget& content);
    void start_watcher();
    bool poll_file();
    bool file_matches_snapshot();
    void reload_from_disk();
    void prompt_reload();
    void on_editor_scroll();
    void on_preview_scroll();
    void notify_save_state();

    SessionController* controller_;
    std::string note_id_;
    std::string title_;
    NoteEditor* editor_{nullptr};
    MarkdownPreview* preview_{nullptr};
    Gtk::Paned* content_split_{nullptr};
    Gtk::Box* content_host_{nullptr};
    std::function<void()> on_save_state_;
    std::function<void(const std::filesystem::path&)> on_file_saved_;

    bool sync_scroll_{false};
    bool syncing_{false};

    std::string watched_path_;
    std::filesystem::file_time_type last_mtime_;
    std::uintmax_t last_size_{0};
    bool triggered_{false};
    bool last_modified_{false};
    sigc::connection watcher_timer_;
    sigc::connection dirty_debounce_;  // coalesces keystrokes → live unsaved dot
};

} // namespace remin::gui
