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
#include <set>
#include <string>
#include <vector>

namespace remin::gui {

class SessionController;
class MainWindow;

// A terminal tab: mirrors the core PaneTree into a tree of Gtk::Paned widgets,
// each leaf holding a TerminalPane (VTE). Split/resize/remove go through the
// SessionController; the widget tree is only a projection of core state.
class TerminalTabView : public TabView {
public:
    TerminalTabView(SessionController* controller,
                    MainWindow* main_window,
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

    void add_history(const std::string& command);

    // Host callback invoked by the pane's right-click "Color Profile…" item.
    // MainWindow wires this to its color profile dialog.
    void set_color_request_callback(std::function<void()> cb) {
        on_color_request_ = std::move(cb);
    }

    // Callback for opening a file in the note editor (from directory panel).
    void set_open_file_callback(std::function<void(const std::filesystem::path&)> cb) {
        on_open_file_ = std::move(cb);
    }

    // Callback for command history — MainWindow wires this to its global sidebar.
    void set_history_callback(std::function<void(const std::string&)> cb) {
        on_history_ = std::move(cb);
    }

    // Apply colors to all terminal panes in this tab
    void set_all_pane_colors(const Gdk::RGBA& fg, const Gdk::RGBA& bg) {
        for (auto& [id, pane] : panes_) {
            pane->set_colors(fg, bg);
        }
    }

    // Callback to request closing the entire tab (when last pane is closed)
    void set_close_tab_request_callback(std::function<void()> cb) {
        on_close_tab_request_ = std::move(cb);
    }

private:
    void rebuild();
    void sync_ratio(Gtk::Paned& paned, const std::string& first_child_pane);
    Gtk::Widget& build_node(const remin::core::PaneTree& node);
    void activate_pane(const remin::core::PaneId& pane);
    void show_pane_menu(Gtk::Widget& pane_widget, double x, double y);
    void load_and_apply_saved_colors();

    SessionController* controller_;
    MainWindow* main_window_;
    remin::core::WindowId window_;
    remin::core::TabId tab_;
    remin::core::PaneId root_pane_;
    std::string title_;
    std::string shell_;

    std::map<std::string, std::unique_ptr<TerminalPane>> panes_;
    std::set<std::string> pane_controllers_added_;  // Track which panes have gesture controllers
    remin::core::PaneId active_pane_;
    std::function<void()> on_color_request_;
    std::function<void(const std::filesystem::path&)> on_open_file_;
    std::function<void(const std::string&)> on_history_;

    // Callback to request closing the entire tab (when last pane is closed)
    std::function<void()> on_close_tab_request_;

    Gtk::Box* tree_host_{nullptr};
    Gtk::Popover* pane_menu_{nullptr};
};

} // namespace remin::gui
