#pragma once

#include <gtkmm.h>
#include <adwaita.h>
#include <vte/vte.h>
#include <string>

namespace remin::gui {

// A single terminal pane backed by a VTE widget. VTE provides the PTY,
// scrollback, selection, and terminal emulation — Remin doesn't reinvent it.
//
// Each TerminalPane owns its own VteTerminal and shell child process.
class TerminalPane {
public:
    TerminalPane(const std::string& shell, const std::string& cwd);
    ~TerminalPane();

    TerminalPane(const TerminalPane&) = delete;
    TerminalPane& operator=(const TerminalPane&) = delete;

    // The widget to place in a container.
    Gtk::Widget& widget();

    // Capture current scrollback (text) for snapshotting.
    std::string capture_scrollback();

    // Feed user keystrokes / paste into the terminal.
    void feed(std::string_view data);

    void set_title(std::string title) { title_ = std::move(title); }
    const std::string& title() const { return title_; }

private:
    VteTerminal* vte_{nullptr};   // owned by Gtk::Widget
    Gtk::Widget* widget_{nullptr};
    std::string shell_;
    std::string cwd_;
    std::string title_;
};

} // namespace remin::gui
