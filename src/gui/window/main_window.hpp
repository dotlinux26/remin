#pragma once

#include "core/workspace_core.hpp"
#include "gui/terminal/terminal_pane.hpp"

#include <gtkmm.h>
#include <memory>
#include <vector>
#include <string>

namespace remin::gui {

// Main Remin GUI window.
//
// Layout (text-based navigation, no icon soup):
//
//  ┌──────────────────────────────────────────┐
//  │ Remin   <Workspace>              <N tabs>│  <- header bar
//  ├──────────────────────────────────────────┤
//  │ Recon  Source  Exploit  Notes           │  <- tab strip (text)
//  ├──────────────────────────────────────────┤
//  │                 terminal                 │  <- panes (VTE)
//  ├──────────────────────────────────────────┤
//  │ ~/gitlab-audit                     bash  │  <- status bar
//  └──────────────────────────────────────────┘
class MainWindow : public Gtk::Window {
public:
    explicit MainWindow();
    ~MainWindow() override;

private:
    void add_tab();
    void on_reload();

    Gtk::Box* root_{nullptr};
    Gtk::HeaderBar* header_{nullptr};
    Gtk::Label* header_info_{nullptr};
    Gtk::Box* tab_strip_{nullptr};
    Gtk::Box* pane_area_{nullptr};
    Gtk::Label* status_info_{nullptr};

    std::vector<std::unique_ptr<TerminalPane>> panes_;
    int active_tab_{-1};
};

} // namespace remin::gui
