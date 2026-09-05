#include "gui/window/settings_dialog.hpp"

#include <adwaita.h>
#include <giomm/settings.h>
#include <gtkmm.h>
#include <gdk/gdk.h>

namespace remin::gui {

SettingsDialog::SettingsDialog(Gtk::Window& parent, SessionController* controller)
    : Gtk::Dialog("Settings", parent, true), controller_(controller) {
    set_default_size(560, 420);
    set_resizable(true);

    // Ensure dialog gets the same theme as the main window
    add_css_class("remin-window");

    notebook_ = Gtk::make_managed<Gtk::Notebook>();
    notebook_->set_hexpand(true);
    notebook_->set_vexpand(true);
    get_content_area()->append(*notebook_);

    setup_appearance_page();
    setup_terminal_page();
    setup_editor_page();
    setup_behavior_page();

    show();
}

void SettingsDialog::setup_appearance_page() {
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    page->set_margin(16);

    auto* title = Gtk::make_managed<Gtk::Label>("Appearance");
    title->add_css_class("title-2");
    title->set_halign(Gtk::Align::START);
    page->append(*title);

    auto* theme_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    theme_box->set_halign(Gtk::Align::START);
    auto* theme_label = Gtk::make_managed<Gtk::Label>("Dark theme");
    theme_label->set_valign(Gtk::Align::CENTER);
    
    // Get current dark mode from AdwStyleManager
    AdwStyleManager* style_manager = adw_style_manager_get_default();
    bool is_dark = adw_style_manager_get_color_scheme(style_manager) == ADW_COLOR_SCHEME_PREFER_DARK;
    
    dark_theme_switch_ = Gtk::make_managed<Gtk::Switch>();
    dark_theme_switch_->set_active(is_dark);
    dark_theme_switch_->set_valign(Gtk::Align::CENTER);
    dark_theme_switch_->property_active().signal_changed().connect(
        [this]() {
            if (dark_theme_switch_) {
                AdwStyleManager* sm = adw_style_manager_get_default();
                adw_style_manager_set_color_scheme(sm, dark_theme_switch_->get_active() ? ADW_COLOR_SCHEME_PREFER_DARK : ADW_COLOR_SCHEME_FORCE_LIGHT);
            }
            if (controller_) {
                controller_->set_theme_dark(dark_theme_switch_->get_active());
            }
        });
    theme_box->append(*theme_label);
    theme_box->append(*dark_theme_switch_);
    page->append(*theme_box);

    notebook_->append_page(*page, "Appearance");
}

void SettingsDialog::setup_terminal_page() {
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    page->set_margin(16);

    auto* title = Gtk::make_managed<Gtk::Label>("Terminal");
    title->add_css_class("title-2");
    title->set_halign(Gtk::Align::START);
    page->append(*title);

    // Color Profile is now managed globally via the terminal's header menu.
    // Changes there apply to ALL terminal panes across ALL tabs.
    auto* info_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    info_box->set_margin(12);
    auto* info_label = Gtk::make_managed<Gtk::Label>(
        "Terminal colors are managed globally. Open any terminal tab and use "
        "the header menu → Color Profile to change colors for ALL terminal panes "
        "across all tabs.");
    info_label->set_wrap(true);
    info_label->set_halign(Gtk::Align::START);
    info_label->add_css_class("dim-label");
    info_box->append(*info_label);
    page->append(*info_box);

    notebook_->append_page(*page, "Terminal");
}

void SettingsDialog::setup_editor_page() {
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    page->set_margin(16);

    auto* title = Gtk::make_managed<Gtk::Label>("Editor");
    title->add_css_class("title-2");
    title->set_halign(Gtk::Align::START);
    page->append(*title);

    auto* info = Gtk::make_managed<Gtk::Label>(
        "Editor settings (font, tab width, line wrapping) will be added here.");
    info->set_wrap(true);
    info->set_halign(Gtk::Align::START);
    info->add_css_class("dim-label");
    page->append(*info);

    notebook_->append_page(*page, "Editor");
}

void SettingsDialog::setup_behavior_page() {
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    page->set_margin(16);

    auto* title = Gtk::make_managed<Gtk::Label>("Behavior");
    title->add_css_class("title-2");
    title->set_halign(Gtk::Align::START);
    page->append(*title);

    auto* ar_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    ar_box->set_halign(Gtk::Align::START);
    auto* ar_label = Gtk::make_managed<Gtk::Label>("Auto-reload changed files");
    ar_label->set_valign(Gtk::Align::CENTER);
    autoreload_switch_ = Gtk::make_managed<Gtk::Switch>();
    autoreload_switch_->set_active(controller_ ? controller_->auto_reload_enabled() : false);
    autoreload_switch_->set_valign(Gtk::Align::CENTER);
    autoreload_switch_->property_active().signal_changed().connect(
        [this]() {
            if (autoreload_switch_ && controller_) {
                controller_->set_auto_reload_enabled(autoreload_switch_->get_active());
            }
        });
    ar_box->append(*ar_label);
    ar_box->append(*autoreload_switch_);
    page->append(*ar_box);

    notebook_->append_page(*page, "Behavior");
}

bool SettingsDialog::on_theme_changed(bool dark) {
    AdwStyleManager* sm = adw_style_manager_get_default();
    adw_style_manager_set_color_scheme(sm, dark ? ADW_COLOR_SCHEME_PREFER_DARK : ADW_COLOR_SCHEME_FORCE_LIGHT);
    return true;
}

bool SettingsDialog::on_autoreload_changed(bool enabled) {
    if (controller_) {
        controller_->set_auto_reload_enabled(enabled);
    }
    return true;
}

void SettingsDialog::on_color_foreground_changed() {
    // Color change is previewed immediately on the active terminal pane
    // The actual save happens when user clicks "Save Color Profile"
}

void SettingsDialog::on_color_background_changed() {
    // Same as foreground - preview only
}

void SettingsDialog::save_color_profile() {
    if (!fg_color_btn_ || !bg_color_btn_ || !controller_) return;

    Gdk::RGBA fg = fg_color_btn_->get_rgba();
    Gdk::RGBA bg = bg_color_btn_->get_rgba();

    // Convert RGBA to CSS color string (e.g., "rgb(255, 255, 255)")
    std::string fg_str = fg.to_string();
    std::string bg_str = bg.to_string();

    SessionController::ColorProfile profile;
    profile.foreground = fg_str;
    profile.background = bg_str;
    controller_->set_color_profile(profile);

    // Also apply to all current terminal panes
    // This would need MainWindow integration - for now just save
}

} // namespace remin::gui