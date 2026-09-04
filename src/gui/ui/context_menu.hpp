#pragma once

#include <gtkmm.h>
#include <functional>
#include <string>
#include <vector>

namespace remin::gui {

/// Reusable context menu (right-click popup).
/// Builder-style API — call add_item / add_separator / add_label, then show().
///
/// Usage:
///   ContextMenu::show(widget, x, y, {
///       {"Copy",  [&]{ vte_copy(); }},
///       {"Paste", [&]{ vte_paste(); }},
///   });
class ContextMenu {
public:
    struct Item {
        std::string label;
        std::function<void()> on_click;
        bool sensitive = true;   // false = greyed out
    };

    /// Special label value to insert a horizontal separator.
    static constexpr const char* SEPARATOR_LABEL = "---";

    /// Show a context menu anchored at (x, y) relative to `widget`.
    /// The popover is automatically destroyed after dismissal.
    static void show(Gtk::Widget& anchor, double x, double y,
                     const std::vector<Item>& items);

    /// Show with explicit parent (for widgets that can't be a popover parent).
    static void show_at(Gtk::Widget& parent, double x, double y,
                        const std::vector<Item>& items);
};

} // namespace remin::gui
