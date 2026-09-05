#include "gui/application.hpp"

#include <adwaita.h>
#include <gtkmm.h>

namespace remin::gui {

Application::Application()
    : Gtk::Application("io.github.remin.remin"),
      session_(std::make_unique<WorkspaceSession>()) {}

void Application::on_activate() {
    if (!session_->ok()) {
        g_warning("remin: session init failed: %s", session_->error().c_str());
        return;
    }

    // Initialize AdwStyleManager for dark/light mode management
    style_manager_ = adw_style_manager_get_default();

    // AdwStyleManager only switches the adwaita base palette. Remin's own
    // layout/spacing rules live in resources/styles/{dark,light}.css, so they
    // must be loaded explicitly here (otherwise every control falls back to
    // the default Adwaita look: rounded buttons, visible button chrome, ...).
    css_provider_ = Gtk::CssProvider::create();
    if (auto* gdk_display = gdk_display_get_default()) {
        auto display = Glib::wrap(gdk_display, true);
        Gtk::StyleContext::add_provider_for_display(
            display, css_provider_, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    // Reload the matching CSS whenever the scheme changes (toolbar dark action,
    // settings switch, or reload action all funnel through AdwStyleManager).
    g_signal_connect(style_manager_, "notify::color-scheme",
                     G_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
                         static_cast<Application*>(self)->apply_theme_css(
                             adw_style_manager_get_dark(
                                 static_cast<Application*>(self)->style_manager_));
                     }),
                     this);

    // Register bundled icons GResource so GTK can resolve all symbolic icon
    // names without depending on system icon themes (Adwaita/Yaru/etc.).
    {
        std::string icons_path = std::string(REMIN_RESOURCE_DIR) + "/icons/icons.gresource";
        GError* err = nullptr;
        GResource* res = g_resource_load(icons_path.c_str(), &err);
        if (res) {
            g_resources_register(res);
            auto* display = GDK_DISPLAY(gdk_display_get_default());
            if (display) {
                auto* icon_theme = gtk_icon_theme_get_for_display(display);
                // Use /icons/hicolor so GTK finds icons in scalable/actions/, scalable/ui/, etc.
                gtk_icon_theme_add_resource_path(icon_theme, "/icons/hicolor");
            }
        } else {
            g_warning("remin: failed to load icons resource: %s", err ? err->message : "unknown");
            if (err) g_error_free(err);
        }
    }

    // Load persisted theme preference, or fall back to system theme.
    auto* ctrl = session_->controller();
    bool dark = false;
    if (ctrl) {
        dark = ctrl->theme_dark();
    }
    if (!ctrl || !dark) {
        // No persisted preference, use system theme
        auto settings = Gtk::Settings::get_default();
        if (settings) dark = settings->property_gtk_application_prefer_dark_theme();
    }
    adw_style_manager_set_color_scheme(style_manager_, dark ? ADW_COLOR_SCHEME_PREFER_DARK : ADW_COLOR_SCHEME_FORCE_LIGHT);
    apply_theme_css(dark);

    auto* win = new MainWindow(ctrl, session_->autosaver(), session_->core());
    window_ = win;
    add_window(*win);

    // Scrollback provider for terminal panes
    session_->autosaver()->set_scrollback_provider(
        [this](const remin::core::PaneId& pane) -> std::optional<std::string> {
            if (!window_) return std::nullopt;
            for (auto* tab : window_->terminal_tabs()) {
                if (auto text = tab->capture(pane)) return text;
            }
            return std::nullopt;
        });

    // Note provider for note tabs
    session_->autosaver()->set_note_provider(
        [this](const std::string& noteId) -> std::optional<std::string> {
            if (!window_) return std::nullopt;
            for (auto* tab : window_->note_tabs()) {
                if (tab->id() == noteId) return tab->text();
            }
            return std::nullopt;
        });

    // Lightweight poll: check for due autosave every 250ms
    constexpr unsigned int poll_ms = 250;
    Glib::signal_timeout().connect(
        sigc::mem_fun(*this, &Application::on_autosave_tick), poll_ms);

    win->present();
}

bool Application::on_autosave_tick() {
    if (session_->autosaver() && session_->autosaver()->due()) {
        bool ok = session_->autosaver()->flush();
        if (window_) window_->show_autosave_badge(ok);
    }
    return true;
}

void Application::apply_theme_css(bool dark) {
    if (!css_provider_) return;
    std::string path = std::string(REMIN_RESOURCE_DIR) + "/styles/" +
                       (dark ? "dark" : "light") + ".css";
    try {
        css_provider_->load_from_path(path);
    } catch (const Glib::Error& e) {
        g_warning("remin: failed to load theme css %s: %s", path.c_str(),
                  e.what());
    }
}

} // namespace remin::gui