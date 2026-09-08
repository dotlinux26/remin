#include "gui/window/commands_page.hpp"

#include "gui/terminal/terminal_pane.hpp"
#include "gui/terminal/pane_history_tracker.hpp"

#include <gtkmm.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

namespace remin::gui {

CommandsPage::CommandsPage()
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {

    add_css_class("remin-commands-page");

    // Search entry at top
    search_entry_.set_placeholder_text("Search commands...");
    search_entry_.add_css_class("remin-commands-search");
    search_entry_.set_hexpand(true);
    search_entry_.signal_changed().connect([this]() {
        search_text_ = search_entry_.get_text();
        apply_search_filter();
    });
    append(search_entry_);

    // Scrolled list
    scroller_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_vexpand(true);
    scroller_.set_child(list_box_);
    append(scroller_);

    set_vexpand(true);
}

void CommandsPage::set_tracker(PaneHistoryTracker* tracker) {
    tracker_ = tracker;
    refresh();
}

void CommandsPage::set_target_pane(TerminalPane* pane) {
    target_pane_ = pane;
}

void CommandsPage::refresh() {
    rows_.clear();
    if (!tracker_) {
        schedule_rebuild();
        return;
    }
    auto cmds = tracker_->commands_desc();
    rows_.reserve(cmds.size());
    for (const auto& rec : cmds) {
        CommandRow row;
        row.full_command = rec.command;
        row.display_text = truncate_for_display(rec.command);
        row.timestamp_us = rec.timestamp_us;
        row.pinned = rec.pinned;
        row.pane_id = current_pane_id_;
        row.window_id = current_window_id_;
        rows_.push_back(std::move(row));
    }
    schedule_rebuild();
}

void CommandsPage::schedule_rebuild() {
    if (rebuild_scheduled_) return;
    rebuild_scheduled_ = true;
    Glib::signal_idle().connect_once([this]() {
        rebuild_scheduled_ = false;
        rebuild_list();
    });
}

void CommandsPage::set_pane_context(const std::string& pane_id, const std::string& window_id) {
    current_pane_id_ = pane_id;
    current_window_id_ = window_id;
}

void CommandsPage::set_search_text(const std::string& text) {
    search_entry_.set_text(text);
}

void CommandsPage::rebuild_list() {
    while (auto* child = list_box_.get_first_child()) {
        list_box_.remove(*child);
    }

    if (rows_.empty()) {
        // Show placeholder when no commands (matches Transcripts pattern)
        auto* placeholder = Gtk::make_managed<Gtk::Label>(
            "No commands yet.\n\nCommands will appear here after you run them in the terminal.\n\n"
            "Each pane has its own isolated history (HISTFILE).");
        placeholder->set_wrap(true);
        placeholder->set_xalign(0.5);
        placeholder->set_yalign(0.5);
        placeholder->add_css_class("remin-placeholder-text");
        placeholder->set_margin_top(24);
        placeholder->set_margin_bottom(24);
        placeholder->set_margin_start(12);
        placeholder->set_margin_end(12);
        list_box_.append(*placeholder);
        return;
    }

    // Sort: pinned first (newest first within pinned), then unpinned (newest first)
    std::stable_sort(rows_.begin(), rows_.end(),
        [](const CommandRow& a, const CommandRow& b) {
            if (a.pinned != b.pinned) return a.pinned; // pinned first
            return false; // stable_sort preserves original order (newest first)
        });

    for (auto& row : rows_) {
        // Apply search filter
        if (!search_text_.empty()) {
            std::string haystack = row.full_command;
            std::string needle = search_text_;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            std::transform(needle.begin(), needle.end(), needle.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (haystack.find(needle) == std::string::npos) {
                continue;
            }
        }

        auto* btn = Gtk::make_managed<Gtk::Button>();
        btn->add_css_class("remin-history-item");
        btn->set_halign(Gtk::Align::FILL);
        
        // Build tooltip with command and timestamp
        std::string tooltip = row.full_command;
        if (row.timestamp_us > 0) {
            auto time_t = std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::time_point(
                    std::chrono::microseconds(row.timestamp_us)));
            char time_buf[64];
            std::strftime(time_buf, sizeof(time_buf), "\nExecuted: %Y-%m-%d %H:%M:%S", std::localtime(&time_t));
            tooltip += time_buf;
        }
        btn->set_tooltip_text(tooltip);

        // Build row content: pin icon + command text
        auto* hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        hbox->set_hexpand(true);

        // Pin indicator
        if (row.pinned) {
            auto* pin_icon = Gtk::make_managed<Gtk::Image>();
            pin_icon->set_from_icon_name("object-locked-symbolic");
            pin_icon->set_pixel_size(14);
            pin_icon->add_css_class("remin-pin-indicator");
            hbox->append(*pin_icon);
        }

        auto* label = Gtk::make_managed<Gtk::Label>(row.display_text);
        label->set_xalign(0.0);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_hexpand(true);
        label->add_css_class("remin-command-label");
        hbox->append(*label);

        btn->set_child(*hbox);

        // Left click: insert command
        btn->signal_clicked().connect([this, cmd = row.full_command]() {
            on_row_clicked(cmd);
        });

        // Right click: context menu
        auto click = Gtk::GestureClick::create();
        click->set_button(3); // Right click
        click->signal_pressed().connect([this, btn, cmd = row.full_command](int, double x, double y) {
            on_row_right_clicked(cmd, x, y, btn);
        });
        btn->add_controller(click);

        row.button = btn;
        list_box_.append(*btn);
    }
}

void CommandsPage::apply_search_filter() {
    search_text_ = search_entry_.get_text();
    schedule_rebuild();
}

void CommandsPage::on_row_clicked(const std::string& command) {
    on_insert_(command);
}

void CommandsPage::on_row_right_clicked(const std::string& command, double x, double y, Gtk::Button* button) {
    show_context_menu(command, x, y, button);
}

void CommandsPage::show_context_menu(const std::string& command, double x, double y, Gtk::Button* button) {
    if (!target_pane_ || !button) return;

    auto* popover = Gtk::make_managed<Gtk::Popover>();
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_margin(4);

    auto make_item = [&](const std::string& label, std::function<void()> action) {
        auto* btn = Gtk::make_managed<Gtk::Button>(label);
        btn->add_css_class("remin-context-menu-item");
        btn->set_halign(Gtk::Align::FILL);
        btn->signal_clicked().connect([popover, action = std::move(action)]() {
            action();
            popover->popdown();
        });
        box->append(*btn);
    };

    // Copy command
    make_item("Copy command", [this, command]() {
        on_copy_(command);
    });

    // Insert command
    make_item("Insert command", [this, command]() {
        on_insert_(command);
    });

    // Pin/Unpin
    auto it = std::find_if(rows_.begin(), rows_.end(),
                          [&](const CommandRow& r) { return r.full_command == command; });
    bool is_pinned = (it != rows_.end() && it->pinned);
    make_item(is_pinned ? "Unpin" : "Pin", [this, command, is_pinned]() {
        on_pin_(command, !is_pinned);
        if (tracker_) {
            tracker_->pin_command(command, !is_pinned);
        }
        refresh();
    });

    popover->set_child(*box);
    popover->set_parent(*button);  // Anchor to the row button that was clicked
    popover->set_has_arrow(false); // Remove the pointing arrow
    popover->set_pointing_to(Gdk::Rectangle{static_cast<int>(x), static_cast<int>(y), 1, 1});
    popover->popup();
}

std::string CommandsPage::truncate_for_display(const std::string& cmd, std::size_t max_len) {
    if (cmd.size() <= max_len) return cmd;
    return cmd.substr(0, max_len - 3) + "...";
}

} // namespace remin::gui