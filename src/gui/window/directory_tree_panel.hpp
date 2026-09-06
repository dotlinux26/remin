#pragma once

#include <gtkmm.h>

#include <filesystem>
#include <functional>
#include <set>
#include <string>

#include "core/workspace/workspace.hpp"

namespace remin::gui {

// VS Code-style file tree panel shown in the "Files" sidebar page.
//
// View/model split inside this class:
//   State  = the model. Filenames, expanded set, selection, filter and the
//            scroll anchor live here (path-identity based), NOT in transient
//            row widgets. Stays authoritative across rebuilds.
//   Widget = the view. Rows are a rendering of State; widgets are rebuilt any
//            time State changes, but State decides how they come back.
//
// Reconcile policy:
//   * refresh()            - stateful top-level rebuild (preserves expansion,
//                            selection, filter, scroll anchor).
//   * reconcile_directory  - refresh ONLY the subtree of one directory.
//   * update_file_row      - in-place size/mtime update, no rebuild at all.
//   Save note via on_note_saved() never triggers a blind full refresh; it
//   touches only the affected directory row.
class DirectoryTreePanel : public Gtk::Box {
public:
    // Called when the user double-clicks a regular file to open it in the editor.
    using OpenFileCallback = std::function<void(const std::filesystem::path&)>;

    explicit DirectoryTreePanel(OpenFileCallback open_file);

    // Starts at $HOME (per audit issue #1: not the process cwd).
    void set_root(const std::filesystem::path& dir);
    [[nodiscard]] const std::filesystem::path& current_dir() const { return state_.current_dir; }

    // --- Runtime state capture/restore (design §8) ---
    // Capture current directory tree state for checkpoint persistence.
    // Uses core::DirectoryTreeState for the state model.
    [[nodiscard]] remin::core::DirectoryTreeState capture_state() const;
    void apply_state(const remin::core::DirectoryTreeState& state);

    void refresh();
    void focus_search();

    // Refresh and also scroll the tree to the very top afterwards. Used by the
    // Reload button, Home navigation and fresh launches.
    void refresh_to_top() {
        force_scroll_top_ = true;
        refresh();
    }

    // Manual refresh trigger for external UI (e.g. a reload button).
    void reload() { refresh(); }

    // Host notification: a note was written to `saved_path`. Out of interest
    // for the directory tree it must never nuke the user's context — this
    // updates the affected row in place, or reconciles only the parent dir.
    void on_note_saved(const std::filesystem::path& saved_path);

private:
    // ---- Model (path-identity based, survives widget rebuilds) ----
    struct State {
        std::filesystem::path current_dir;
        std::string filter;
        std::set<std::filesystem::path> expanded;  // directories currently expanded
        std::filesystem::path selected;            // currently selected row
        // Scroll anchor: row path sitting at the viewport top + its pixel
        // offset, so rebuilds restore the exact visible position.
        std::filesystem::path anchor;
        double anchor_offset{0.0};
    };

    bool matches_filter(const std::string& name) const;
    Gtk::Widget* create_row(const std::string& name, bool is_dir, const std::filesystem::path& full_path);
    void populate_children(Gtk::Box* children_box, const std::filesystem::path& dir_path);
    void rebuild_top_level();

    // Targeted reconcile: refresh only the subtree rooted at `dir`.
    void reconcile_directory(const std::filesystem::path& dir);

    // In-place row update (no tree rebuild at all).
    void update_file_row(const std::filesystem::path& path);

    void show_context_menu(Gtk::Widget& widget, const std::filesystem::path& path, const std::string& name, bool is_dir, double x, double y);
    void create_new_file(const std::filesystem::path& dir);
    void create_new_folder(const std::filesystem::path& dir);
    void rename_item(const std::filesystem::path& path);
    void delete_item(const std::filesystem::path& path);
    void show_error(Gtk::Window& win, const std::string& message);

    // Scroll the viewport so `path`'s row becomes visible (deferred until the
    // rebuilt tree is laid out). With `only_if_off_screen`, the scroll is left
    // untouched when the row already fits in the viewport.
    void reveal_path(const std::filesystem::path& path, bool only_if_off_screen = false);

    // After a create in `dir`, expand dir + ancestors, rebuild (scroll kept),
    // select and reveal the freshly created item.
    void reveal_after_create(const std::filesystem::path& dir,
                             const std::filesystem::path& new_path);

    // Rebuild the tree from the filesystem. Called on demand and on file
    // change events (never by polling).
    void start_monitor(const std::filesystem::path& dir);
    void schedule_refresh();

    // Selection helpers.
    void set_selected(const std::filesystem::path& path);
    void apply_selection_visual();

    // Scroll anchor helpers.
    void capture_scroll_anchor();
    void restore_scroll_anchor();

    // Widget-tree helpers. Each row container carries "remin-path" object data;
    // "remin-box" points at its clickable row, "remin-children" at its children
    // box (dirs), "remin-size" at its file-size label (files).
    Gtk::Widget* find_container(const std::filesystem::path& path);
    void collect_containers(Gtk::Widget* parent, std::vector<Gtk::Widget*>& out);

    OpenFileCallback open_file_;

    Gtk::SearchEntry* search_{nullptr};
    Gtk::ScrolledWindow* scroller_{nullptr};
    Gtk::Box* tree_{nullptr};
    Gtk::Label* path_label_{nullptr};
    Glib::RefPtr<Gio::FileMonitor> monitor_;
    sigc::connection debounce_;        // 150ms coalescing timer for FS events
    sigc::connection restore_idle_;    // defers scroll restore until layout done
    sigc::connection reveal_idle_;     // defers reveal of a freshly created item

    State state_;
    bool force_scroll_top_{false};
    bool first_show_pending_{true};  // one refresh-to-top on the first map

    static std::filesystem::path get_home_dir() {
        const char* home = std::getenv("HOME");
        return home ? std::filesystem::path(home) : std::filesystem::path("/");
    }
};

} // namespace remin::gui