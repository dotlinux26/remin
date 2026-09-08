#pragma once

#include "core/pane/pane.hpp"
#include "core/terminal/cwd.hpp"

#include <gtkmm.h>
#include <string>
#include <functional>
#include <optional>

struct _VteTerminal;
typedef struct _VteTerminal VteTerminal;

namespace remin::gui {

// Lightweight wrapper around a VTE GTK4 terminal widget.
// Encapsulates PTY spawn, scrollback, and input detection.
class TerminalPane {
public:
    // `history_file` (optional): absolute path to this pane's dedicated shell
    // history file. When set, the pane spawns its shell with HISTFILE pointed
    // there so shell ↑/↓ recall only this pane's commands (§6.1 isolation).
    // If `defer_spawn` is true, the shell is NOT spawned in the constructor.
    // The caller must call either `initialize_fresh()` or `runtime_restore()`
    // to spawn the shell exactly once.
    TerminalPane(const std::string& shell, const std::string& cwd,
                 const std::string& history_file = "",
                 bool defer_spawn = false);
    ~TerminalPane();

    // Initialize a fresh terminal (used for new tabs). Spawns the shell.
    void initialize_fresh();

    // Restore terminal state from a persisted PaneState (used for restore).
    // Restores the binary VTE snapshot then spawns shell exactly once.
    void runtime_restore(const remin::core::PaneState& state);

    // Access the widget for embedding in a container.
    Gtk::Widget& widget();

    // Feed input from the host side (e.g. copied text).
    void feed(std::string_view data);

    // -- Runtime persistence adapters (design §3.1/§5/§4) --
    // Capture the VTE's current runtime state as pure data. The host routes
    // this through the SessionController into canonical PaneState.
    // command_history is intentionally left empty here (see its comment in
    // TerminalRuntimeSnapshot): canonical history lives in core.
    [[nodiscard]] remin::core::TerminalRuntimeSnapshot runtime_capture() const;
    // Deterministic restore: resize → restore binary snapshot → spawn the
    // shell in the captured cwd (design §5.2). Snapshot restore runs BEFORE
    // spawn. (Declaration above as runtime_restore)

    // The short title shown in the tab strip.
    [[nodiscard]] const char* title() const { return title_.c_str(); }

    // Called on every text commit (keystroke). The host wires this to the
    // autosave's note_activity.
    void set_input_callback(std::function<void()> cb) { on_input_ = std::move(cb); }

    // Called once per completed command line (VTE "commit" chunk ending in a
    // newline), trimmed, with the observation timestamp. The host uses this to
    // build the per-pane canonical command history.
    void set_command_callback(std::function<void(remin::core::CommandRecord)> cb) {
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
    // Spawn callback: records the shell's PID (for the §4 /proc/<pid>/cwd
    // fallback).
    static void on_spawned_trampoline(VteTerminal* terminal, GPid pid,
                                      GError* error, gpointer user_data);

    // Spawn the configured shell in `cwd` (empty cwd → shell default).
    void spawn_shell(const std::string& cwd);

    // Resolve the directory the shell currently stands in (design §4.2):
    // OSC 7 URI → /proc/<shell_pid>/cwd → cached → $HOME.
    std::string resolve_capture_cwd() const;
    // Restore-side decision: spawn where the captured dir still exists, else
    // $HOME (design §4.2).
    static std::string resolve_restore_cwd(const std::string& captured);
    // Re-apply VTE-native session props (title / dir uri / file uri) as OSC
    // feeds on the display before the fresh shell spawns (P0-H4).
    void restore_session_metadata(const remin::core::PaneState& state);

    std::string shell_;
    std::string cwd_;
    // Dedicated HISTFILE for this pane (empty = inherit parent env).
    std::string history_file_;
    // Spawn envp (lives as long as the pane; freed in the destructor).
    gchar** envp_{nullptr};
    // Last observed cwd; updated by runtime_capture() (const) so the §4
    // cached fallback survives repeated capture without a live OSC 7 shell.
    mutable std::string cached_cwd_;
    std::string title_;
    long shell_pid_{0};
    VteTerminal* vte_{nullptr};
    Gtk::Widget* widget_{nullptr};
    std::function<void()> on_input_;
    std::function<void(remin::core::CommandRecord)> on_command_;
    std::string commit_buf_;
    std::string last_command_;
    std::optional<remin::core::InterruptedCommand> interrupted_;
};

} // namespace remin::gui
