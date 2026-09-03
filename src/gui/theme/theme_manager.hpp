#pragma once

#include <gtkmm.h>
#include <string>

namespace remin::gui {

// Loads and applies the Remin shell theme (light/dark) as a GTK CSS provider.
//
// The CSS lives in resources/styles/{light,dark}.css and defines the palette
// with GTK's @define-color syntax (GTK does not implement CSS var()). Colors
// are derived from the logo's indigo→cyan gradient; no colors are hard-coded
// in C++.
class ThemeManager {
public:
    // resources_dir is the directory containing styles/ and icons/.
    explicit ThemeManager(std::string resources_dir);

    // Apply the theme matching the GNOME/system dark preference.
    void apply_system();

    // Force and return the applied theme. true -> dark, false -> light.
    bool apply(bool dark);

    // Toggle and return the new state (true = dark).
    bool toggle();

    // Ensure a window has the `remin-window` class so the CSS selector matches.
    static void tag_window(Gtk::Window& w);

    [[nodiscard]] bool is_dark() const { return dark_; }

private:
    bool load_file(const std::string& path);

    std::string resources_dir_;
    Glib::RefPtr<Gtk::CssProvider> provider_;
    bool dark_{false};
};

} // namespace remin::gui
