#pragma once

#include "core/workspace_core.hpp"
#include "gui/window/main_window.hpp"

#include <gtkmm.h>

namespace remin::gui {

// gtkmm application for Remin GUI.
class Application : public Gtk::Application {
public:
    Application();

protected:
    void on_activate() override;
};

} // namespace remin::gui
