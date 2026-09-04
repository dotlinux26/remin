#pragma once

#include <gtkmm.h>
#include <string>

namespace remin::gui {

// Semantic tab kind. The tab icon is only the *presentation* of this value,
// never business logic — dropping icons later must not change architecture.
enum class TabKind {
    Terminal,
    Note,
    // future: Log, Diff, Search, Ssh, ...
};

// Common lifecycle for every tab type. New kinds implement this interface and
// are registered without rewriting MainWindow.
class TabView : public Gtk::Box {
public:
    TabView() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
        set_hexpand(true);
        set_vexpand(true);
    }
    ~TabView() override = default;

    [[nodiscard]] virtual TabKind kind() const = 0;
    [[nodiscard]] virtual const std::string& title() const = 0;
    [[nodiscard]] virtual const std::string& id() const = 0;
    // Rename the tab (updates what the tab strip shows).
    virtual void set_title(const std::string& title) = 0;

    // Grab focus in the active editor / terminal.
    virtual void activate() = 0;
    virtual void deactivate() {}
    // Ctrl+F / Ctrl+H dispatch target returns true if handled.
    virtual bool focus_search() = 0;

    // Toggle the tab's resizable left sidebar (history / outline). Default is a
    // no-op for tabs that do not expose one.
    virtual void toggle_sidebar() {}
    virtual void clear_sidebar() {}
};

// Map a semantic kind to its theme icon name (presentation only).
inline const char* tab_kind_icon(TabKind kind) {
    switch (kind) {
        case TabKind::Terminal: return "utilities-terminal-symbolic";
        case TabKind::Note: return "text-x-generic-symbolic";
    }
    return "remin";
}

} // namespace remin::gui
