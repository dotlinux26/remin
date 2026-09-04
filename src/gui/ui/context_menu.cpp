#include "gui/ui/context_menu.hpp"

namespace remin::gui {

void ContextMenu::show(Gtk::Widget& anchor, double x, double y,
                       const std::vector<Item>& items) {
    // Simple Gtk::Popover with buttons — guaranteed to work, styleable via CSS.
    // Matches editor's native right-click menu look when styled correctly.
    auto* popover = Gtk::make_managed<Gtk::Popover>();
    popover->set_has_arrow(false);
    popover->set_position(Gtk::PositionType::BOTTOM);
    popover->add_css_class("remin-context-menu");

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->add_css_class("remin-context-menu-box");

    for (const auto& item : items) {
        if (item.label == ContextMenu::SEPARATOR_LABEL) {
            auto* sep = Gtk::make_managed<Gtk::Separator>();
            sep->add_css_class("remin-context-menu-separator");
            box->append(*sep);
            continue;
        }

        auto* btn = Gtk::make_managed<Gtk::Button>(item.label);
        btn->add_css_class("remin-context-menu-item");
        btn->set_sensitive(item.sensitive);
        btn->set_halign(Gtk::Align::START);
        btn->set_hexpand(true);
        auto cb = item.on_click;
        btn->signal_clicked().connect([cb, popover]() {
            if (cb) cb();
            popover->popdown();
        });
        box->append(*btn);
    }

    popover->set_child(*box);
    popover->set_parent(anchor);

    auto* rect = new Gdk::Rectangle();
    rect->set_x(static_cast<int>(x));
    rect->set_y(static_cast<int>(y));
    rect->set_width(1);
    rect->set_height(1);
    popover->set_pointing_to(*rect);
    delete rect;

    popover->popup();
}

void ContextMenu::show_at(Gtk::Widget& parent, double x, double y,
                          const std::vector<Item>& items) {
    show(parent, x, y, items);
}

} // namespace remin::gui