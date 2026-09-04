#include "gui/window/settings_dialog.hpp"

#include <giomm/settings.h>
#include <gtkmm.h>
#include <gdk/gdk.h>

namespace remin::gui {

SettingsDialog::SettingsDialog(Gtk::Window& parent, ThemeManager* theme, SessionController* controller)
    : Gtk::Dialog("Settings", parent, true), theme_(theme), controller_(controller) {
    set_default_size(560, 420);
    set_resizable(true);
    add_button("Close", Gtk::ResponseType::CLOSE);
    set_default_response(Gtk::ResponseType::CLOSE);

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
    dark_theme_switch_ = Gtk::make_managed<Gtk::Switch>();
    dark_theme_switch_->set_active(theme_->is_dark());
    dark_theme_switch_->set_valign(Gtk::Align::CENTER);
    dark_theme_switch_->property_active().signal_changed().connect(
        [this]() {
            if (dark_theme_switch_ && theme_) {
                theme_->apply(dark_theme_switch_->get_active());
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

    auto* colors_frame = Gtk::make_managed<Gtk::Frame>("Color Profile");
    colors_frame->set_hexpand(true);
    auto* colors_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    colors_box->set_margin(12);
    colors_frame->set_child(*colors_box);

    auto* fg_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    fg_box->set_halign(Gtk::Align::START);
    auto* fg_label = Gtk::make_managed<Gtk::Label>("Foreground");
    fg_label->set_valign(Gtk::Align::CENTER);
    fg_label->set_size_request(120, -1);
    fg_color_btn_ = Gtk::make_managed<Gtk::ColorButton>();
    fg_color_btn_->set_use_alpha(false);
    fg_color_btn_->signal_color_set().connect(
        sigc::mem_fun(*this, &SettingsDialog::on_color_foreground_changed));
    fg_box->append(*fg_label);
    fg_box->append(*fg_color_btn_);
    colors_box->append(*fg_box);

    auto* bg_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    bg_box->set_halign(Gtk::Align::START);
    auto* bg_label = Gtk::make_managed<Gtk::Label>("Background");
    bg_label->set_valign(Gtk::Align::CENTER);
    bg_label->set_size_request(120, -1);
    bg_color_btn_ = Gtk::make_managed<Gtk::ColorButton>();
    bg_color_btn_->set_use_alpha(false);
    bg_color_btn_->signal_color_set().connect(
        sigc::mem_fun(*this, &SettingsDialog::on_color_background_changed));
    bg_box->append(*bg_label);
    bg_box->append(*bg_color_btn_);
    colors_box->append(*bg_box);

    auto* save_colors = Gtk::make_managed<Gtk::Button>("Save Color Profile");
    save_colors->set_halign(Gtk::Align::START);
    save_colors->signal_clicked().connect(
        sigc::mem_fun(*this, &SettingsDialog::save_color_profile));
    colors_box->append(*save_colors);

    // Load saved color profile
    if (controller_) {
        auto profile = controller_->color_profile();
        if (profile) {
            if (!profile->foreground.empty()) {
                Gdk::RGBA fg;
                gdk_rgba_parse(fg.gobj(), profile->foreground.c_str());
                fg_color_btn_->set_rgba(fg);
            }
            if (!profile->background.empty()) {
                Gdk::RGBA bg;
                gdk_rgba_parse(bg.gobj(), profile->background.c_str());
                bg_color_btn_->set_rgba(bg);
            }
        }
    }

    page->append(*colors_frame);
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
    if (theme_) {
        theme_->apply(dark);
    }
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