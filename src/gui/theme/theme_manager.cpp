#include "gui/theme/theme_manager.hpp"

#include <fstream>
#include <sstream>

namespace remin::gui {

ThemeManager::ThemeManager(std::string resources_dir)
    : resources_dir_(std::move(resources_dir)) {}

bool ThemeManager::load_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    try {
        provider_ = Gtk::CssProvider::create();
        provider_->load_from_data(ss.str());
        return true;
    } catch (const Glib::Error&) {
        provider_.reset();
        return false;
    }
}

bool ThemeManager::apply(bool dark) {
    const std::string path =
        resources_dir_ + "/styles/" + (dark ? "dark.css" : "light.css");
    if (!load_file(path)) return false;
    dark_ = dark;

    auto display = Gdk::Display::get_default();
    if (!display) return false;
    Gtk::StyleProvider::add_provider_for_display(
        display, provider_, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    return true;
}

void ThemeManager::apply_system() {
    bool dark = false;
    auto settings = Gtk::Settings::get_default();
    if (settings) {
        dark = settings->property_gtk_application_prefer_dark_theme();
    }
    apply(dark);
}

bool ThemeManager::toggle() {
    return apply(!dark_);
}

void ThemeManager::tag_window(Gtk::Window& w) {
    if (!w.has_css_class("remin-window")) {
        w.add_css_class("remin-window");
    }
}

} // namespace remin::gui
