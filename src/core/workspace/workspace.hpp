#pragma once

#include "core/id.hpp"
#include "core/window/window.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace remin::core {

// Layout state of the directory-tree panel. Captured by the GUI runtime and
// stored here (canonical), then re-applied on restore.
struct DirectoryTreeState {
    std::filesystem::path current_dir;
    std::vector<std::filesystem::path> expanded;
    std::filesystem::path selected;
    std::string filter;
    std::filesystem::path scroll_anchor;
    double anchor_offset{0.0};
};

// UI state that is part of a workspace checkpoint.
struct UiState {
    DirectoryTreeState directory_tree;
};

// A workspace: the top-level unit. Contains windows, snapshots, history.
struct Workspace {
    // Version of the JSON state schema. Bump + migrate when the serialized
    // structure changes (TabKind, NoteTabState, PluginState, …). Distinct from
    // `generation`, which counts checkpoints.
    static constexpr int kSchemaVersion = 1;

    WorkspaceId id;
    std::string name;
    std::string working_directory;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_saved_at;
    std::vector<std::string> tags;

    int schema_version{kSchemaVersion};
    std::uint64_t generation{0};

    std::vector<Window> windows;
    std::optional<WindowId> focus_window_id;
    std::vector<SnapshotId> snapshot_ids;

    UiState ui;

    Workspace() = default;
    static Workspace create(std::string name, WorkspaceId id = WorkspaceId::generate()) {
        Workspace w;
        w.id = std::move(id);
        w.name = std::move(name);
        w.created_at = std::chrono::system_clock::now();
        w.last_saved_at = w.created_at;
        return w;
    }
};

// One command-history entry with provenance: which window → tab → pane ran it.
// The History sidebar is an aggregate query over the whole workspace's panes
// (design §6.3) — a view, not a second store. `record` keeps the command text
// plus its timestamp; identity is carried by the window/tab/pane fields.
struct HistoryEntry {
    WindowId window;
    TabId tab;
    PaneId pane;
    CommandRecord record;
};

// Aggregate every pane's canonical `command_history` in window → tab → pane →
// command order. Terminal-only (note tabs carry no pane tree history).
[[nodiscard]] inline std::vector<HistoryEntry> aggregate_command_history(const Workspace& ws) {
    std::vector<HistoryEntry> out;
    for (const auto& w : ws.windows) {
        for (const auto& t : w.tabs) {
            std::vector<const Pane*> panes;
            t.pane_tree.collect_panes(panes);
            for (const auto* p : panes) {
                if (!p) continue;
                for (const auto& cmd : p->state.command_history) {
                    out.push_back({w.id, t.id, p->id, cmd});
                }
            }
        }
    }
    return out;
}

// A closed-window history entry: captured when a window is closed with
// Window History policy ON. Contains the full window state at close time.
struct ClosedWindowSnapshot {
    SnapshotId id;
    WindowId window_id;   // original window identity
    std::string label;    // window label at close time
    std::chrono::system_clock::time_point closed_at;
    std::string workspace_state_json;  // serialized window state (tabs, panes, geometry, focus)
    std::uint64_t generation{0};       // workspace generation at close
};

} // namespace remin::core
