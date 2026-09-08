#include "gui/terminal/terminal_pane.hpp"

#include <vte/vte.h>
#include <vte/vteregex.h>
#include <glibmm.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>

namespace remin::gui {

TerminalPane::TerminalPane(const std::string& shell, const std::string& cwd,
                            const std::string& history_file,
                            bool defer_spawn)
    : shell_(shell), cwd_(cwd), history_file_(history_file), title_("terminal") {

    // Container (gtkmm): a simple box that will own the native VTE widget.
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_hexpand(true);
    box->set_vexpand(true);

    // Native VTE terminal (C API). VTE owns PTY + scrollback + selection.
    vte_ = (VteTerminal*) vte_terminal_new();

    // Ensure all terminals start in the user's home directory by default.
    if (cwd_.empty()) {
        if (const char* home = std::getenv("HOME")) cwd_ = home;
    }
    cached_cwd_ = cwd_;

    // Terminal profile: transparent background, semantic colors come from CSS.
    vte_terminal_set_scrollback_lines(vte_, 10000);

    // Scrolling UX: scroll back to the prompt after each keystroke, and let
    // Shift+Up/Down + Shift+PageUp/PageDown scroll the full scrollback when the
    // shell app has not bound those keys (fallback scrolling).
    vte_terminal_set_scroll_on_keystroke(vte_, TRUE);
    vte_terminal_set_scroll_on_output(vte_, TRUE);
    vte_terminal_set_enable_fallback_scrolling(vte_, TRUE);

    // Make the native VTE expand to fill its container (fixes the white gap
    // under the root terminal when its box grows but VTE stays at natural size).
    gtk_widget_set_hexpand(GTK_WIDGET(vte_), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(vte_), TRUE);

    // Embed the native VTE into the gtkmm box via C API on gobj().
    gtk_box_append(GTK_BOX(box->gobj()), GTK_WIDGET(vte_));

    // Connect the commit signal for input detection (edge-triggered autosave).
    g_signal_connect(GTK_WIDGET(vte_), "commit",
                     G_CALLBACK(on_commit_trampoline), this);

    // Ctrl+Shift+C copy selection, Ctrl+Shift+V paste (like most terminals).
    GtkEventController* copy_ctrl = gtk_event_controller_key_new();
    g_signal_connect(copy_ctrl, "key-pressed", G_CALLBACK(on_key_pressed), this);
    gtk_widget_add_controller(GTK_WIDGET(vte_), copy_ctrl);

    // Store a pointer to the managed box as our widget.
    widget_ = box;
    // Hold an extra ref so the widget survives tree_host_->remove() during
    // rebuilds. Without this, removing the widget from its parent drops the
    // ref to 0 and GTK frees it, leaving widget_ dangling.
    g_object_ref(widget_->gobj());

    // Spawn shell unless deferred (used for restore path where runtime_restore
    // will handle feed-then-spawn).
    if (!defer_spawn) {
        spawn_shell(cwd_);
    }
}

TerminalPane::~TerminalPane() {
    if (envp_) {
        g_strfreev(envp_);
        envp_ = nullptr;
    }
    if (widget_) {
        // Release the extra ref we took in the constructor.
        // If the widget is still parented (normal operation), the parent holds
        // its own ref so this just decrements. If unparented, this frees it.
        auto* g = widget_->gobj();
        widget_ = nullptr;
        g_object_unref(g);
    }
}

Gtk::Widget& TerminalPane::widget() {
    return *widget_;
}

void TerminalPane::spawn_shell(const std::string& cwd) {
    if (!vte_) return;
    const char* shell_argv[] = { shell_.c_str(), nullptr };

    // Build the spawn environment once: inherit the parent env, then point
    // HISTFILE at this pane's dedicated file. Kept alive on the object because
    // spawn is asynchronous and GLib does not copy envp.
    if (!envp_) {
        envp_ = g_get_environ();
        if (!history_file_.empty()) {
            envp_ = g_environ_setenv(envp_, "HISTFILE", history_file_.c_str(), TRUE);
        }
    }

    vte_terminal_spawn_async(
        vte_,
        VTE_PTY_DEFAULT,
        cwd.empty() ? nullptr : cwd.c_str(),
        const_cast<char**>(shell_argv),
        envp_,
        G_SPAWN_SEARCH_PATH,
        nullptr, nullptr, nullptr, -1, nullptr,
        &TerminalPane::on_spawned_trampoline, this);
    if (!cwd.empty()) cwd_ = cwd;
}

void TerminalPane::on_spawned_trampoline(VteTerminal*, GPid pid,
                                         GError*, gpointer user_data) {
    auto* self = static_cast<TerminalPane*>(user_data);
    self->shell_pid_ = static_cast<long>(pid);
}

std::string TerminalPane::resolve_capture_cwd() const {
    // 1. OSC 7 — the shell's own report of $PWD (URI form).
    const char* uri = vte_ ? vte_terminal_get_current_directory_uri(vte_) : nullptr;
    const std::string osc7 = uri ? remin::core::terminal::osc7_file_uri_to_path(uri) : "";
    // 2. /proc/<shell_pid>/cwd — only the shell's own cwd, never a child's.
    const std::string proc = remin::core::terminal::read_proc_cwd(shell_pid_);
    // 3+4. cached value, then $HOME.
    const char* home = std::getenv("HOME");
    return remin::core::terminal::pick_capture_cwd(osc7, proc, cached_cwd_,
                                                   home ? home : "");
}

std::string TerminalPane::resolve_restore_cwd(const std::string& captured) {
    const char* home = std::getenv("HOME");
    return remin::core::terminal::pick_restore_cwd(captured, home ? home : "");
}

remin::core::TerminalRuntimeSnapshot TerminalPane::runtime_capture() const {
    remin::core::TerminalRuntimeSnapshot snap;
    if (!vte_) return snap;
    snap.cols = static_cast<std::uint32_t>(vte_terminal_get_column_count(vte_));
    snap.rows = static_cast<std::uint32_t>(vte_terminal_get_row_count(vte_));
    snap.shell = shell_;
    // Capture the full terminal state (scrollback + visible + cursor) as an
    // opaque binary blob via the VTE snapshot API. Remin only carries the
    // GBytes; it never interprets the contents.
    GBytes* blob = nullptr;
    vte_terminal_snapshot_capture(vte_, &blob);
    if (blob) {
        gsize n = 0;
        const void* data = g_bytes_get_data(blob, &n);
        if (data && n > 0) {
            const auto* p = static_cast<const std::uint8_t*>(data);
            snap.snapshot_data.assign(p, p + n);
        }
        g_bytes_unref(blob);
    }
    snap.cwd = resolve_capture_cwd();
    if (!snap.cwd.empty()) cached_cwd_ = snap.cwd;
    // Session metadata (P0-H4): VTE-native props not carried in the snapshot
    // blob (title / dir uri / file uri). Getter returns nullptr when unset.
    if (const char* t = vte_terminal_get_window_title(vte_)) snap.window_title = t;
    if (const char* u = vte_terminal_get_current_directory_uri(vte_))
        snap.current_directory_uri = u;
    if (const char* u = vte_terminal_get_current_file_uri(vte_))
        snap.current_file_uri = u;
    snap.interrupted_command = interrupted_;
    return snap;
}

// Initialize a fresh terminal (used for new tabs). Spawns the shell.
void TerminalPane::initialize_fresh() {
    if (!vte_) return;
    // Already configured in constructor, just spawn the shell.
    if (cwd_.empty()) {
        if (const char* home = std::getenv("HOME")) cwd_ = home;
    }
    spawn_shell(cwd_);
}

// Restore terminal state from a persisted PaneState (used for restore).
// Restores the binary VTE snapshot then spawns shell exactly once.
void TerminalPane::runtime_restore(const remin::core::PaneState& state) {
    if (!vte_) return;

    // Empty PaneState → first build is a fresh shell (nothing to restore).
    if (state.snapshot_data.empty() && state.cols == 0 && state.rows == 0 &&
        state.cwd.empty() && state.shell.empty() && state.window_title.empty() &&
        state.current_directory_uri.empty() && state.current_file_uri.empty()) {
        // Nothing to restore, but we still need a shell if this is a fresh pane.
        // However, if we got here via restore_pane_tree(), the constructor was
        // called with defer_spawn=true, so we need to spawn.
        initialize_fresh();
        return;
    }

    // 1. Grid size first (design §5.2): the snapshot wraps to the captured
    //    dimensions. Widget must be configured before restore.
    if (state.cols > 0 && state.rows > 0) {
        vte_terminal_set_size(vte_, static_cast<glong>(state.cols),
                              static_cast<glong>(state.rows));
    }

    // 2. Restore the binary snapshot BEFORE spawning the shell. The captured
    //    VTE state is fully re-created (scrollback, visible region, cursor);
    //    the shell is spawned fresh afterwards so it lands "below" the content.
    if (!state.snapshot_data.empty()) {
        GBytes* blob = g_bytes_new(state.snapshot_data.data(),
                                   static_cast<gsize>(state.snapshot_data.size()));
        const bool ok = vte_terminal_snapshot_restore(vte_, blob);
        g_bytes_unref(blob);
        if (!ok) {
            g_printerr("REMIN: snapshot restore failed for pane\n");
        } else {
            // The restored screen's last line holds the old shell's prompt and
            // the cursor sits right after it. The freshly spawned shell will
            // print its own prompt at the cursor position, gluing two prompts
            // together. Move the cursor to a clean CR-LF line (display-only,
            // NOT fed to the child) so the new prompt lands on its own row.
            const char nl[] = "\r\n";
            vte_terminal_feed(vte_, nl, sizeof(nl) - 1);
        }
    }

    // 2.5 Session metadata (P0-H4): re-apply VTE-native title/dir/file URI as
    //     OSC sequences (display-only feeds — parsed by VTE, NOT sent to the
    //     child). OSC 2 = window title, OSC 7 = current dir URI, OSC 6 =
    //     current file URI. The snapshot blob does NOT carry these.
    restore_session_metadata(state);

    // 3. Spawn a fresh shell (env inherits the default environment, §3.2) in
    //    the captured cwd if it still exists.
    if (!state.shell.empty()) shell_ = state.shell;
    cached_cwd_ = state.cwd;
    spawn_shell(resolve_restore_cwd(state.cwd));
}

void TerminalPane::restore_session_metadata(const remin::core::PaneState& state) {
    if (!vte_) return;
    const auto feed_osc = [this](std::string_view osc) {
        vte_terminal_feed(vte_, osc.data(), static_cast<gssize>(osc.size()));
    };
    if (!state.window_title.empty()) {
        const std::string osc = "\033]2;" + state.window_title + "\a";
        feed_osc(osc);
    }
    if (!state.current_directory_uri.empty()) {
        const std::string osc = "\033]7;" + state.current_directory_uri + "\a";
        feed_osc(osc);
    }
    if (!state.current_file_uri.empty()) {
        const std::string osc = "\033]6;" + state.current_file_uri + "\a";
        feed_osc(osc);
    }
}

void TerminalPane::feed(std::string_view data) {
    if (vte_) {
        vte_terminal_feed(vte_, data.data(), static_cast<gssize>(data.size()));
    }
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

bool TerminalPane::has_selection() const {
    return vte_ && vte_terminal_get_has_selection(vte_);
}

void TerminalPane::copy_clipboard() {
    if (vte_) vte_terminal_copy_clipboard_format(vte_, VTE_FORMAT_TEXT);
}

void TerminalPane::paste_clipboard() {
    if (vte_) vte_terminal_paste_clipboard(vte_);
}

void TerminalPane::select_all() {
    if (vte_) vte_terminal_select_all(vte_);
}

void TerminalPane::clear_scrollback() {
    if (vte_) vte_terminal_reset(vte_, TRUE, TRUE);
}

void TerminalPane::on_commit_trampoline(GtkWidget*, const char* text, guint size, gpointer user_data) {
    auto* self = static_cast<TerminalPane*>(user_data);
    if (self->on_input_) self->on_input_();
    if (!text || size == 0) return;

    // Interrupted command (design §6.2): a literal \x03 in a commit right after
    // a completed line is evidence the user pressed Ctrl+C on that command.
    // Only here do we ever claim source = CtrlC — never guessed otherwise.
    const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    if (std::memchr(text, 0x03, static_cast<std::size_t>(size)) &&
        !self->last_command_.empty()) {
        self->interrupted_ = remin::core::InterruptedCommand{
            self->last_command_, now_us, remin::core::InterruptedCommand::Source::CtrlC};
    }

    // Accumulate committed text and emit each completed line as a command.
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
        if (!line.empty()) {
            self->last_command_ = line;
            if (self->on_command_) {
                self->on_command_(remin::core::CommandRecord{line, now_us});
            }
        }
    }
}

gboolean TerminalPane::on_key_pressed(GtkEventControllerKey*, guint keyval,
                                      guint keycode, GdkModifierType state, gpointer user_data) {
    auto* self = static_cast<TerminalPane*>(user_data);
    if (!self || !self->vte_) return FALSE;

    const bool ctrl = (state & GDK_CONTROL_MASK) != 0;
    const bool shift = (state & GDK_SHIFT_MASK) != 0;
    if (!ctrl || !shift) return FALSE;

    switch (keyval) {
        case GDK_KEY_C:
        case GDK_KEY_c: {
            // Copy the current selection into the clipboard.
            if (vte_terminal_get_has_selection(self->vte_)) {
                vte_terminal_copy_clipboard_format(self->vte_, VTE_FORMAT_TEXT);
            }
            return TRUE;
        }
        case GDK_KEY_V:
        case GDK_KEY_v: {
            vte_terminal_paste_clipboard(self->vte_);
            return TRUE;
        }
        default:
            return FALSE;
    }
}

} // namespace remin::gui
