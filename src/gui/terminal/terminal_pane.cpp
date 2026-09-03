#include "gui/terminal/terminal_pane.hpp"

#include <glibmm.h>
#include <cstring>

namespace remin::gui {

TerminalPane::TerminalPane(const std::string& shell, const std::string& cwd)
    : shell_(shell), cwd_(cwd), title_("terminal") {

    // Container (gtkmm): a simple box that will own the native VTE widget.
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_hexpand(true);
    box->set_vexpand(true);

    // Native VTE terminal (C API). VTE owns PTY + scrollback + selection.
    vte_ = (VteTerminal*) vte_terminal_new();

    // Spawn the shell into the terminal's PTY.
    const char* shell_argv[] = { shell_.c_str(), nullptr };
    char** envp = nullptr;
    vte_terminal_spawn_async(
        vte_,
        VTE_PTY_DEFAULT,
        cwd_.empty() ? nullptr : cwd_.c_str(),
        const_cast<char**>(shell_argv),
        envp,
        G_SPAWN_SEARCH_PATH,
        nullptr, nullptr, nullptr, -1, nullptr, nullptr, nullptr);

    // Terminal profile: transparent background, semantic colors come from CSS.
    vte_terminal_set_scrollback_lines(vte_, 10000);
    vte_terminal_set_rewrap_on_resize(vte_, TRUE);

    // Embed the native VTE into the gtkmm box via C API on gobj().
    gtk_box_append(GTK_BOX(box->gobj()), GTK_WIDGET(vte_));

    // Store a pointer to the managed box as our widget (box is Gtk::make_managed).
    widget_ = box;
}

TerminalPane::~TerminalPane() = default;

Gtk::Widget& TerminalPane::widget() {
    return *widget_;
}

void TerminalPane::feed(std::string_view data) {
    // Feed bytes from the host side (e.g. user's copied text) to the terminal.
    if (vte_) {
        vte_terminal_feed(vte_, data.data(), static_cast<gssize>(data.size()));
    }
}

std::string TerminalPane::capture_scrollback() {
    if (!vte_) return {};
    char* text = vte_terminal_get_text(vte_, nullptr, nullptr, nullptr);
    std::string out = text ? text : "";
    if (text) g_free(text);
    return out;
}

} // namespace remin::gui
