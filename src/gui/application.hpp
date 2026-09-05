#pragma once

#include "gui/session/workspace_session.hpp"
#include "gui/window/main_window.hpp"

#include <adwaita.h>
#include <gtkmm.h>
#include <memory>

namespace remin::gui {

class Application : public Gtk::Application {
public:
    Application();

protected:
    void on_activate() override;

private:
    bool on_autosave_tick();
    void apply_theme_css(bool dark);

    std::unique_ptr<WorkspaceSession> session_;
    AdwStyleManager* style_manager_{nullptr};
    Glib::RefPtr<Gtk::CssProvider> css_provider_;
    MainWindow* window_{nullptr};
    bool autosave_ok_{true};
};

} // namespace remin::gui