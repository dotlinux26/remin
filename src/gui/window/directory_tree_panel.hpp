#pragma once

#include <gtkmm.h>

#include <filesystem>
#include <functional>
#include <string>

namespace remin::gui {

// VS Code-style file tree panel shown in the "Files" sidebar page.
//
// Extracted from the former MainWindow God Object so the directory browser has
// one owner for its own state (current directory, filter, expand state) and one
// shared row/context-menu style. It never touches WorkspaceCore/Storage; it only
// filesystem-operates and reports open-file requests via a callback the host
// (MainWindow) supplies.
class DirectoryTreePanel : public Gtk::Box {
public:
    // Called when the user double-clicks a regular file to open it in the editor.
    using OpenFileCallback = std::function<void(const std::filesystem::path&)>;

    explicit DirectoryTreePanel(OpenFileCallback open_file);

    // Starts at $HOME (per audit issue #1: not the process cwd).
    void set_root(const std::filesystem::path& dir);
    [[nodiscard]] const std::filesystem::path& current_dir() const { return current_dir_; }

    void refresh();
    void focus_search();

    // Manual refresh trigger for external UI (e.g. a reload button / after a
    // note is saved by the editor).
    void reload() { refresh(); }

private:
    bool matches_filter(const std::string& name) const;
    Gtk::Widget* create_row(const std::string& name, bool is_dir, const std::filesystem::path& full_path);
    void populate_children(Gtk::Box* children_box, const std::filesystem::path& dir_path);
    void show_context_menu(Gtk::Widget& widget, const std::filesystem::path& path, const std::string& name, bool is_dir);
    void create_new_file(const std::filesystem::path& dir);
    void create_new_folder(const std::filesystem::path& dir);
    void rename_item(const std::filesystem::path& path);
    void delete_item(const std::filesystem::path& path);

    // Rebuild the tree from the filesystem. Called on demand and on file
    // change events (never by polling).
    void start_monitor(const std::filesystem::path& dir);
    void schedule_refresh();

    OpenFileCallback open_file_;

    Gtk::SearchEntry* search_{nullptr};
    Gtk::ScrolledWindow* scroller_{nullptr};
    Gtk::Box* tree_{nullptr};
    Gtk::Label* path_label_{nullptr};
    Glib::RefPtr<Gio::FileMonitor> monitor_;
    sigc::connection debounce_;   // 150ms coalescing timer for FS events
    std::filesystem::path current_dir_;
    std::string filter_;

    static std::filesystem::path get_home_dir() {
        const char* home = std::getenv("HOME");
        return home ? std::filesystem::path(home) : std::filesystem::path("/");
    }
};

} // namespace remin::gui
