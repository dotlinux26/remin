#include "gui/application.hpp"

#include <gtkmm.h>

namespace remin::gui {

Application::Application()
    : Gtk::Application("io.github.remin.remin") {}

void Application::on_activate() {
    auto* win = new MainWindow();
    add_window(*win);
    win->present();
}

} // namespace remin::gui
