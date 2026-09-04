#pragma once

#include "gui/theme/theme_manager.hpp"
#include "gui/session/session_controller.hpp"

#include <gtkmm.h>
#include <memory>

namespace remin::gui {

class SettingsDialog : public Gtk::Dialog {
public:
    SettingsDialog(Gtk::Window& parent, ThemeManager* theme, SessionController* controller);
    ~SettingsDialog() override = default;

private:
    void setup_appearance_page();
    void setup_terminal_page();
    void setup_editor_page();
    void setup_behavior_page();

    bool on_theme_changed(bool dark);
    bool on_autoreload_changed(bool enabled);
    void on_color_foreground_changed();
    void on_color_background_changed();
    void save_color_profile();

    ThemeManager* theme_{nullptr};
    SessionController* controller_{nullptr};

    Gtk::Notebook* notebook_{nullptr};

    // Appearance page
    Gtk::Switch* dark_theme_switch_{nullptr};

    // Terminal page
    Gtk::ColorButton* fg_color_btn_{nullptr};
    Gtk::ColorButton* bg_color_btn_{nullptr};

    // Behavior page
    Gtk::Switch* autoreload_switch_{nullptr};
};

} // namespace remin::gui