#include "gui/terminal/terminal_pane.hpp"

#include <vte/vte.h>
#include <vte/vteregex.h>
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

    // Scrolling UX: scroll back to the prompt after each keystroke, and let
    // Shift+Up/Down + Shift+PageUp/PageDown scroll the full scrollback when the
    // shell app has not bound those keys (fallback scrolling).
    vte_terminal_set_scroll_on_keystroke(vte_, TRUE);
    vte_terminal_set_scroll_on_output(vte_, TRUE);
    vte_terminal_set_enable_fallback_scrolling(vte_, TRUE);

    // Embed the native VTE into the gtkmm box via C API on gobj().
    gtk_box_append(GTK_BOX(box->gobj()), GTK_WIDGET(vte_));

    // Connect the commit signal for input detection (edge-triggered autosave).
    g_signal_connect(GTK_WIDGET(vte_), "commit",
                     G_CALLBACK(on_commit_trampoline), this);

    // Store a pointer to the managed box as our widget.
    widget_ = box;
}

TerminalPane::~TerminalPane() = default;

Gtk::Widget& TerminalPane::widget() {
    return *widget_;
}

void TerminalPane::feed(std::string_view data) {
    if (vte_) {
        vte_terminal_feed(vte_, data.data(), static_cast<gssize>(data.size()));
    }
}

std::string TerminalPane::capture_scrollback() {
    if (!vte_) return {};
    char* text = vte_terminal_get_text_format(vte_, VTE_FORMAT_TEXT);
    std::string out = text ? text : "";
    if (text) g_free(text);
    return out;
}

void TerminalPane::set_search_text(const std::string& text) {
    if (!vte_) return;
    if (text.empty()) {
        clear_search();
        return;
    }
    // Escape the literal text so user input is treated as plain text, not regex.
    gchar* escaped = g_regex_escape_string(text.c_str(), -1);
    // PCRE2_MULTILINE (0x400) must be in the flags parameter for VTE's
    // runtime check _vte_regex_has_multiline_compile_flag to pass.
    VteRegex* re = vte_regex_new_for_search_full(escaped, -1, 0x00000400u, 0, nullptr, nullptr);
    g_free(escaped);
    if (re) {
        vte_terminal_search_set_regex(vte_, re, 0);
        vte_regex_unref(re);
    }
}

bool TerminalPane::search_next() {
    return vte_ && vte_terminal_search_find_next(vte_);
}

bool TerminalPane::search_previous() {
    return vte_ && vte_terminal_search_find_previous(vte_);
}

void TerminalPane::clear_search() {
    if (vte_) vte_terminal_search_set_regex(vte_, nullptr, 0);
}

void TerminalPane::set_colors(const Gdk::RGBA& fg, const Gdk::RGBA& bg) {
    if (!vte_) return;
    vte_terminal_set_color_foreground(vte_, fg.gobj());
    vte_terminal_set_color_background(vte_, bg.gobj());
}

void TerminalPane::use_default_colors() {
    if (vte_) vte_terminal_set_default_colors(vte_);
}

void TerminalPane::on_commit_trampoline(GtkWidget*, const char* text, guint size, gpointer user_data) {
    auto* self = static_cast<TerminalPane*>(user_data);
    if (self->on_input_) self->on_input_();

    // Accumulate committed text and emit each completed line as a command.
    if (self->on_command_ && text && size > 0) {
        self->commit_buf_.append(text, size);
        std::size_t pos = 0;
        while ((pos = self->commit_buf_.find('\n')) != std::string::npos) {
            std::string line = self->commit_buf_.substr(0, pos);
            self->commit_buf_.erase(0, pos + 1);
            // Trim surrounding whitespace.
            auto b = line.find_first_not_of(" \t\r");
            auto e = line.find_last_not_of(" \t\r");
            if (b != std::string::npos && e != std::string::npos) {
                line = line.substr(b, e - b + 1);
            }
            if (!line.empty()) self->on_command_(line);
        }
    }
}

} // namespace remin::gui
