#pragma once

#include "core/autosave.hpp"
#include "core/workspace_core.hpp"

#include <string>

namespace remin::gui {

// Orchestration layer between the UI and the domain (the "C" in
// View → Controller → Core → Storage/Runtime).
//
// MainWindow never touches WorkspaceCore / Storage / Autosaver directly — it
// emits commands here. Core keeps state + invariants; this controller keeps
// orchestration (multi-step application operations such as open/close/rename,
// terminal/note tab lifecycle, split/resize, snapshot). Autosave policy is
// configured here (one system, per-resource thresholds), never per widget.
// Note bodies live in the same generic storage blob store as scrollback,
// keyed by the note id, routed through Storage — never via a direct file write.
class SessionController {
public:
    SessionController(remin::core::WorkspaceCore* core,
                      remin::core::Storage* storage,
                      remin::core::Autosaver* autosaver);

    [[nodiscard]] remin::core::WorkspaceCore* core() const { return core_; }
    [[nodiscard]] remin::core::Autosaver* autosaver() const { return autosaver_; }

    // -- Workspaces --
    bool open_workspace(const remin::core::WorkspaceId& id);
    bool close_workspace();
    bool rename_workspace(const std::string& name);

    // -- Windows --
    remin::core::WindowId add_window(const std::string& title);
    bool rename_window(const remin::core::WindowId& id, const std::string& title);

    // -- Terminal tabs --
    struct TerminalTab {
        remin::core::WindowId window;
        remin::core::TabId tab;
        remin::core::PaneId root_pane;
    };
    TerminalTab new_terminal_tab(const std::string& title);

    // -- Panes (split/resize) --
    remin::core::PaneId split_pane(const remin::core::TabId& tab,
                                   remin::core::PaneTree::Kind kind,
                                   double ratio = 0.5);
    bool remove_pane(const remin::core::TabId& tab, const remin::core::PaneId& pane);
    bool set_pane_ratio(const remin::core::TabId& tab,
                        const remin::core::PaneId& pane, double ratio);
    // Record the focused pane so split targets the pane the user is on.
    bool focus_pane(const remin::core::TabId& tab, const remin::core::PaneId& pane);

    // Close a tab (delegates to MainWindow)
    bool close_tab(const remin::core::WindowId& window, const remin::core::TabId& tab);

    // -- Notes --
    // Names a new note; its body is persisted/loaded via the storage blob
    // store keyed by the returned id.
    std::string new_note();
    std::string load_note(const std::string& noteId);
    // Create a note tab from existing NoteTabState (for restore).
    std::string restore_note(const remin::core::NoteTabState& state);

    // Explicit file persistence for a note (Save / Save As / temp-file).
    // A note has an optional assigned file path; until it does, an explicit
    // save writes the body to an auto-generated temp file (if autosave-to-temp
    // is enabled) so nothing typed is ever lost.
    [[nodiscard]] std::string note_path(const std::string& noteId) const;
    void set_note_path(const std::string& noteId, const std::string& path);

    // Write note content to an arbitrary file path (used by Save As and the
    // temp-file path). Returns true on success.
    bool write_note_file(const std::string& path, const std::string& content);

    // Deterministic temp path for a note id (used when it has no assigned path).
    [[nodiscard]] std::string note_temp_path(const std::string& noteId) const;

    // Setting: persist each unnamed note's body to its temp file on autosave.
    [[nodiscard]] bool autosave_temp_enabled() const;
    void set_autosave_temp_enabled(bool enabled);

    // Setting: when a note's assigned file changes on disk, reload it silently
    // instead of prompting. Defaults to off (prompt with Reload/Keep).
    [[nodiscard]] bool auto_reload_enabled() const;
    void set_auto_reload_enabled(bool enabled);

    // Setting: auto-open the history/directory sidebar panel on launch.
    // Defaults to off (panel stays hidden until the user opens it).
    [[nodiscard]] bool auto_show_panel_enabled() const;
    void set_auto_show_panel_enabled(bool enabled);

    // Setting: what MainWindow should do when the user closes a note tab that
    // still has unsaved edits.
    //   Ask  - prompt every time (Keep = save & close, Skip = close w/o saving)
    //   Keep - always save (Save As dialog if the note has no file path yet);
    //          the close is aborted if the user cancels saving
    //   Skip - always close without saving
    enum class UnsavedClose { Ask, Keep, Skip };
    [[nodiscard]] UnsavedClose unsaved_close_behavior() const;
    void set_unsaved_close_behavior(UnsavedClose behavior);

    // -- User preferences (theme, color profile) --
    // Dark/light theme preference
    [[nodiscard]] bool theme_dark() const;
    void set_theme_dark(bool dark);

    // Terminal color profile (fg/bg colors)
    struct ColorProfile {
        std::string foreground;
        std::string background;
    };
    [[nodiscard]] std::optional<ColorProfile> color_profile() const;
    void set_color_profile(const ColorProfile& profile);

    // Terminal colors (global fg/bg for all terminal panes)
    [[nodiscard]] std::optional<ColorProfile> terminal_colors() const;
    void set_terminal_colors(const std::string& fg, const std::string& bg);

    // -- Command history (canonical, per-pane, design §6) --
    // Route a completed command into the pane's canonical history (core).
    bool add_command_to_pane(const remin::core::TabId& tab,
                            const remin::core::PaneId& pane,
                            const std::string& command);
    // One-time migration of the legacy global `settings:command-history` blob
    // into the first terminal pane's canonical history (design §6.1: keep the
    // old key only to migrate).
    void migrate_legacy_command_history();
    // Aggregate view over all panes of the current workspace (provenance kept
    // in core; this flattens to command text for the sidebar).
    [[nodiscard]] std::vector<std::string> get_command_history() const;
    // Persist a clear: empty every pane's canonical history.
    bool clear_command_history();

    // -- Snapshots --
    remin::core::SnapshotId create_snapshot();
    bool restore_snapshot(const remin::core::SnapshotId& snap);

    // -- Checkpoint / Persistence --
    // Capture runtime state from all terminal panes and persist an atomic
    // recovery checkpoint (reason="recovery"). Call on shutdown.
    bool checkpoint_recovery();
    // Capture runtime state from all terminal panes and persist an atomic
    // manual checkpoint (reason="manual").
    bool checkpoint_manual();

private:
    // Capture runtime state from all terminal panes in the current workspace
    // and feed into core via apply_runtime_state().
    void capture_all_runtime_state();

    remin::core::WorkspaceCore* core_;
    remin::core::Storage* storage_;
    remin::core::Autosaver* autosaver_;
};

} // namespace remin::gui
