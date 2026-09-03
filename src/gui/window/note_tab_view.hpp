#pragma once

#include "core/id.hpp"
#include "gui/note/markdown_preview.hpp"
#include "gui/note/note_editor.hpp"
#include "gui/window/tab_view.hpp"

#include <filesystem>
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

    // Toggle the markdown preview split (on by default on first split request).
    void toggle_preview();

    [[nodiscard]] std::string text() const { return editor_->text(); }

    // Explicit save now (routes to the session autosaver's flush_now, and — if
    // autosave-to-temp is on and the note has no assigned path — mirrors the
    // body to the note's temp file so nothing is lost).
    void save_now();

    // Save As: assign a file path via a chooser and write the body there.
    void save_as();

    // Set the note's title shown on its tab / window.
    void set_title(const std::string& title) override { title_ = title; }

    // Left resizable sidebar (outline / note meta).
    void toggle_sidebar() override;
    void clear_sidebar() override;

private:
    void connect_editor();
    void build_sidebar();
    void set_content(Gtk::Widget& content);
    void update_sidebar();
    void start_watcher();
    bool poll_file();
    bool file_matches_snapshot();
    void reload_from_disk();
    void prompt_reload();

    SessionController* controller_;
    std::string note_id_;
    std::string title_;
    NoteEditor* editor_{nullptr};
    MarkdownPreview* preview_{nullptr};
    Gtk::Paned* content_split_{nullptr};
    Gtk::Paned* root_paned_{nullptr};
    Gtk::Box* content_host_{nullptr};
    Gtk::Label* sidebar_status_{nullptr};

    std::string watched_path_;
    std::filesystem::file_time_type last_mtime_;
    std::uintmax_t last_size_{0};
    bool triggered_{false};
    sigc::connection watcher_timer_;
};

} // namespace remin::gui
