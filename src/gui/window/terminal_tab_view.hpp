#pragma once

#include "core/workspace_core.hpp"
#include "gui/terminal/terminal_pane.hpp"
#include "gui/window/tab_view.hpp"

#include <filesystem>
#include <gtkmm.h>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace remin::gui {

class SessionController;

// A terminal tab: mirrors the core PaneTree into a tree of Gtk::Paned widgets,
// each leaf holding a TerminalPane (VTE). Split/resize/remove go through the
// SessionController; the widget tree is only a projection of core state.
class TerminalTabView : public TabView {
public:
    TerminalTabView(SessionController* controller,
                    remin::core::WindowId window,
                    remin::core::TabId tab,
                    remin::core::PaneId root_pane);
    ~TerminalTabView() override;

    TabKind kind() const override { return TabKind::Terminal; }
    const std::string& title() const override { return title_; }
    void set_title(const std::string& title) override { title_ = title; }
    const std::string& id() const override { return root_pane_.str(); }
    void activate() override;
    void deactivate() override;
    bool focus_search() override;

    // Split the currently focused pane. Returns the new pane id (or empty).
    remin::core::PaneId split(remin::core::PaneTree::Kind kind);

    // Remove the currently focused pane, or the last remaining pane, and
    // rebuild the paned tree. Returns false if there is nothing to remove.
    bool close_focused_pane();

    // Capture scrollback for a given pane id (pane can belong to this tab).
    std::optional<std::string> capture(const remin::core::PaneId& pane) const;

    // Find a terminal pane widget by id (nullptr if not in this tab).
    TerminalPane* pane(const remin::core::PaneId& pane);

    // The currently focused pane (navigable / target of focused actions).
    TerminalPane* focused_pane();

    // Toggle / clear the tab's resizable left history sidebar.
    void toggle_sidebar() override;
    void clear_sidebar() override; 
    void add_history(const std::string& command);

    // Switch sidebar mode: History or Directory
    void set_sidebar_mode(const std::string& mode); // "history" or "directory"

    // Host callback invoked by the pane's right-click "Color Profile…" item.
    // MainWindow wires this to its color profile dialog.
    void set_color_request_callback(std::function<void()> cb) {
        on_color_request_ = std::move(cb);
    }

    // Callback for opening a file in the note editor (from directory panel).
    void set_open_file_callback(std::function<void(const std::filesystem::path&)> cb) {
        on_open_file_ = std::move(cb);
    }

private:
    void rebuild();
    void sync_ratio(Gtk::Paned& paned, const std::string& first_child_pane);
    // Recursively build Paned/leaf widgets for the given core pane tree node,
    // returning a widget to be inserted.
    Gtk::Widget& build_node(const remin::core::PaneTree& node);
    void activate_pane(const remin::core::PaneId& pane);
    void build_sidebar();
    void update_sidebar();
    void show_pane_menu(Gtk::Widget& pane_widget);
    void refresh_directory();
    bool directory_matches_filter(const std::string& name);
    Gtk::Widget* create_directory_row(const std::string& name, bool is_dir, const std::filesystem::path& full_path);
    void populate_directory_expander(Gtk::Expander* expander, const std::filesystem::path& dir_path);
    void show_directory_context_menu(Gtk::Widget& widget, const std::filesystem::path& path, const std::string& name, bool is_dir);
    void create_new_file(const std::filesystem::path& dir);
    void create_new_folder(const std::filesystem::path& dir);
    void rename_item(const std::filesystem::path& path);
    void delete_item(const std::filesystem::path& path);
    void copy_to_clipboard(const std::string& text);
    bool is_text_file(const std::filesystem::path& path);
    void open_file_in_editor(const std::filesystem::path& path);

    // Apply the current divider ratios to the divider recently dragged.
    SessionController* controller_;
    remin::core::WindowId window_;
    remin::core::TabId tab_;
    remin::core::PaneId root_pane_;
    std::string title_;
    std::string shell_;

    std::map<std::string, std::unique_ptr<TerminalPane>> panes_;
    remin::core::PaneId active_pane_;
    std::function<void()> on_color_request_;
    std::function<void(const std::filesystem::path&)> on_open_file_;

    // Left resizable sidebar (history/directory)
    Gtk::Paned* root_paned_{nullptr};
    Gtk::Box* tree_host_{nullptr};
    Gtk::Stack* sidebar_stack_{nullptr};
    Gtk::ScrolledWindow* history_scroller_{nullptr};
    Gtk::Box* history_list_{nullptr};
    Gtk::ScrolledWindow* directory_scroller_{nullptr};
    Gtk::Box* directory_tree_{nullptr};  // Root of the tree structure
    Gtk::SearchEntry* directory_search_{nullptr};  // Search/filter for directory
    std::string directory_filter_;  // Current filter text
    std::vector<std::string> history_;
    std::filesystem::path current_dir_;
    Gtk::Popover* pane_menu_{nullptr};
};

} // namespace remin::gui
