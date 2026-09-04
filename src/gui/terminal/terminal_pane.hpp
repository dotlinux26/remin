#pragma once

#include <gtkmm.h>
#include <string>
#include <functional>

struct _VteTerminal;
typedef struct _VteTerminal VteTerminal;

namespace remin::gui {

// Lightweight wrapper around a VTE GTK4 terminal widget.
// Encapsulates PTY spawn, scrollback, and input detection.
class TerminalPane {
public:
    TerminalPane(const std::string& shell, const std::string& cwd);
    ~TerminalPane();

    // Access the widget for embedding in a container.
    Gtk::Widget& widget();

    // Feed input from the host side (e.g. copied text).
    void feed(std::string_view data);

    // Read the current scrollback buffer text.
    std::string capture_scrollback();

    // The short title shown in the tab strip.
    [[nodiscard]] const char* title() const { return title_.c_str(); }

    // Called on every text commit (keystroke). The host wires this to the
    // autosave's note_activity.
    void set_input_callback(std::function<void()> cb) { on_input_ = std::move(cb); }

    // Called once per completed command line (VTE "commit" chunk ending in a
    // newline), trimmed. The host uses this to build a command-history sidebar.
    void set_command_callback(std::function<void(std::string)> cb) {
        on_command_ = std::move(cb);
    }

    // -- Find (VTE regex search over the terminal contents) --
    void set_search_text(const std::string& text);
    bool search_next();
    bool search_previous();
    int get_match_count() const;
    bool has_match() const;
    void clear_search();

    // -- Color profile (foreground / background) --
    void set_colors(const Gdk::RGBA& fg, const Gdk::RGBA& bg);
    void use_default_colors();

    // -- Context menu helpers --
    bool has_selection() const;
    void copy_clipboard();
    void paste_clipboard();
    void select_all();
    void clear_scrollback();

private:
    // VTE C callback trampoline.
    static void on_commit_trampoline(GtkWidget* widget, const char* text,
                                     guint size, gpointer user_data);
    // Key handler for Ctrl+Shift+C/V copy/paste.
    static gboolean on_key_pressed(GtkEventControllerKey* controller, guint keyval,
                                   guint keycode, GdkModifierType state, gpointer user_data);

    std::string shell_;
    std::string cwd_;
    std::string title_;
    VteTerminal* vte_{nullptr};
    Gtk::Widget* widget_{nullptr};
    std::function<void()> on_input_;
    std::function<void(std::string)> on_command_;
    std::string commit_buf_;
};

} // namespace remin::gui
