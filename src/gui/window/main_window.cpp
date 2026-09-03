#include "gui/window/main_window.hpp"
#include "terminal/shell/shell.hpp"

#include <gtkmm.h>

namespace remin::gui {

MainWindow::MainWindow() {
    set_title("Remin");

    // Root layout.
    root_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    set_child(*root_);

    // Header: Remin  <workspace>      <N tabs>
    header_ = Gtk::make_managed<Gtk::HeaderBar>();
    set_titlebar(*header_);

    header_info_ = Gtk::make_managed<Gtk::Label>("Remin");
    header_info_->set_selectable(true);
    header_->set_title_widget(*header_info_);

    auto* new_tab_btn = Gtk::make_managed<Gtk::Button>("+ New Tab");
    new_tab_btn->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::add_tab));
    header_->pack_end(*new_tab_btn);

    // Tab strip (text-based).
    tab_strip_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    tab_strip_->set_margin_top(4);
    tab_strip_->set_margin_bottom(4);
    tab_strip_->set_margin_start(8);
    tab_strip_->set_margin_end(8);
    root_->append(*tab_strip_);

    // Pane area.
    pane_area_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    pane_area_->set_hexpand(true);
    pane_area_->set_vexpand(true);
    root_->append(*pane_area_);

    // Status bar.
    status_info_ = Gtk::make_managed<Gtk::Label>("");
    status_info_->set_selectable(true);
    status_info_->set_xalign(0.0f);
    status_info_->set_margin_start(12);
    status_info_->set_margin_top(4);
    status_info_->set_margin_bottom(4);
    root_->append(*status_info_);

    set_default_size(980, 640);

    add_tab();
}

MainWindow::~MainWindow() = default;

void MainWindow::add_tab() {
    const auto shell = remin::terminal::detect_default_shell();
    auto pane = std::make_unique<TerminalPane>(shell, "");

    const int idx = static_cast<int>(panes_.size());
    auto* tab_btn = Gtk::make_managed<Gtk::ToggleButton>(pane->title());
    tab_btn->signal_toggled().connect([this, idx]() {
        active_tab_ = idx;
        on_reload();
    });

    tab_strip_->append(*tab_btn);
    pane_area_->append(pane->widget());

    panes_.push_back(std::move(pane));
    active_tab_ = idx;
    tab_btn->set_active(true);
    on_reload();
}

void MainWindow::on_reload() {
    const int n = static_cast<int>(panes_.size());
    for (int i = 0; i < n; ++i) {
        panes_[static_cast<std::size_t>(i)]->widget().set_visible(i == active_tab_);
    }
    if (status_info_) {
        status_info_->set_text(n == 0 ? "0 tabs" : std::to_string(n) + " tabs");
    }
}

} // namespace remin::gui
