#pragma once

#include "core/pane/pane.hpp"
#include "gui/terminal/terminal_pane.hpp"

#include <gtkmm.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace remin::gui {

class PaneHistoryTracker;

/// Commands page widget: searchable, truncatable command history list with
/// click-to-insert and right-click context menu (Copy/Insert/Pin).
class CommandsPage : public Gtk::Box {
public:
    using InsertCallback = std::function<void(const std::string& command)>;
    using CopyCallback = std::function<void(const std::string& command)>;
    using PinCallback = std::function<void(const std::string& command, bool pinned)>;

    explicit CommandsPage();

    /// Set the history tracker for the currently focused pane.
    void set_tracker(PaneHistoryTracker* tracker);

    /// Set the terminal pane for command insertion (click handler).
    void set_target_pane(TerminalPane* pane);

    /// Set the current pane/window IDs for DB annotation.
    void set_pane_context(const std::string& pane_id, const std::string& window_id);

    /// Refresh the list from the tracker (call on pane focus, history sync, search change).
    void refresh();

    /// Set search filter (live filtering).
    void set_search_text(const std::string& text);

    /// Connect insert callback (host can override default feed_child behavior).
    void set_insert_callback(InsertCallback cb) { on_insert_ = std::move(cb); }

    /// Connect copy callback.
    void set_copy_callback(CopyCallback cb) { on_copy_ = std::move(cb); }

    /// Connect pin callback.
    void set_pin_callback(PinCallback cb) { on_pin_ = std::move(cb); }

private:
    struct CommandRow {
        std::string full_command;
        std::string display_text;  // truncated for label
        std::int64_t timestamp_us = 0;
        bool pinned = false;
        Gtk::Button* button = nullptr;
        Gtk::Popover* popover = nullptr;
        std::string pane_id;
        std::string window_id;
    };

    void rebuild_list();
    void apply_search_filter();
    void on_row_clicked(const std::string& command);
    void on_row_right_clicked(const std::string& command, double x, double y, Gtk::Button* button);
    void show_context_menu(const std::string& command, double x, double y, Gtk::Button* button);
    static std::string truncate_for_display(const std::string& cmd, std::size_t max_len = 80);

    Gtk::SearchEntry search_entry_;
    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_box_{Gtk::Orientation::VERTICAL, 2};

    PaneHistoryTracker* tracker_ = nullptr;
    TerminalPane* target_pane_ = nullptr;
    std::string current_pane_id_;
    std::string current_window_id_;
    std::string search_text_;
    std::vector<CommandRow> rows_;

    InsertCallback on_insert_ = [this](const std::string& cmd) {
        if (target_pane_) target_pane_->feed_child(cmd);
    };
    CopyCallback on_copy_ = [](const std::string& cmd) {
        auto display = Gdk::Display::get_default();
        if (display) {
            auto clipboard = display->get_clipboard();
            clipboard->set_text(cmd);
        }
    };
    PinCallback on_pin_ = [](const std::string&, bool) {};

    void schedule_rebuild();
    bool rebuild_scheduled_ = false;
};

} // namespace remin::gui