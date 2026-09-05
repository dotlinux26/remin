#pragma once

#include "core/workspace/workspace.hpp"
#include "core/snapshot/snapshot.hpp"
#include "core/pane/pane.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace remin::core {

using json = nlohmann::json;

// Interface the WorkspaceCore uses to persist/load state.
// Implemented by storage layer (SQLite). Keeps Core decoupled from SQLite.
class Storage {
public:
    virtual ~Storage() = default;

    // Workspaces
    virtual std::vector<Workspace> list_workspaces() = 0;
    virtual std::optional<Workspace> load_workspace(const WorkspaceId& id) = 0;
    virtual void save_workspace(const Workspace& workspace) = 0;
    virtual void delete_workspace(const WorkspaceId& id) = 0;

    // Snapshots
    virtual std::vector<Snapshot> list_snapshots(const WorkspaceId& id) = 0;
    virtual std::optional<json> load_snapshot(const WorkspaceId& id, const SnapshotId& snap) = 0;
    virtual void save_snapshot(const WorkspaceId& id, const Snapshot& snap, const json& state) = 0;
    virtual void delete_snapshot(const WorkspaceId& id, const SnapshotId& snap) = 0;

    // Scrollback blobs (large binary/text, stored separately from metadata)
    virtual void store_scrollback(const PaneId& pane, std::string content) = 0;
    virtual std::string load_scrollback(const PaneId& pane) = 0;

    // Atomic checkpoint: writes workspace JSON, all scrollback blobs, and a
    // snapshot row in a single transaction. Returns true on success.
    // `generation` is the new generation number (monotonically increasing).
    // `reason` is one of: "recovery", "autosave", "window_history", "manual".
    virtual bool checkpoint(const WorkspaceId& ws_id,
                           const json& workspace_state,
                           int schema_version,
                           int64_t generation,
                           const std::string& reason,
                           const std::vector<std::pair<PaneId, std::string>>& scrollbacks) = 0;
};

// Callback sink for events emitted by WorkspaceCore. GUI/CLI/IPC subscribe.
struct WorkspaceEvent {
    enum class Type {
        WorkspaceOpened,
        WorkspaceClosed,
        WindowAdded,
        WindowRemoved,
        WindowRenamed,
        TabAdded,
        TabRemoved,
        PaneSplit,
        PaneRemoved,
        PaneResized,
        SnapshotCreated,
        StateDirty,
    };
    Type type;
    WorkspaceId workspace;
    std::optional<WindowId> window;
    std::optional<TabId> tab;
    std::optional<PaneId> pane;
};

using WorkspaceEventCallback = std::function<void(const WorkspaceEvent&)>;

// The central engine. All frontends (GUI, CLI, IPC) drive this same API.
class WorkspaceCore {
public:
    explicit WorkspaceCore(Storage* storage);
    ~WorkspaceCore();

    // Event subscription
    void set_event_callback(WorkspaceEventCallback cb) { event_callback_ = std::move(cb); }

    // -- Workspaces --
    WorkspaceId create_workspace(std::string name);
    bool open_workspace(const WorkspaceId& id);
    bool close_workspace();
    [[nodiscard]] const Workspace* current_workspace() const;
    [[nodiscard]] Workspace* current_workspace();
    bool rename_workspace(const WorkspaceId& id, std::string name);
    bool delete_workspace(const WorkspaceId& id);

    // -- Windows --
    WindowId add_window(std::string title);
    bool remove_window(const WindowId& id);
    bool rename_window(const WindowId& id, std::string title);
    bool focus_window(const WindowId& id);

    // -- Tabs --
    TabId add_tab(const WindowId& window, std::string title, PaneTree initial_pane);
    bool remove_tab(const WindowId& window, const TabId& tab);
    bool focus_tab(const WindowId& window, const TabId& tab);

    // -- Panes --
    PaneId split_pane(const TabId& tab, PaneTree::Kind kind, double ratio = 0.5);
    bool remove_pane(const TabId& tab, const PaneId& pane);
    bool focus_pane(const TabId& tab, const PaneId& pane);
    bool set_pane_ratio(const TabId& tab, const PaneId& pane, double ratio);

    // -- Per-pane command history (canonical) --
    // Append a completed command to the pane's history. Adjacent repeats are
    // collapsed and the list is capped (design §6.1). Returns false when the
    // tab/pane does not exist or the command is empty.
    bool add_command_to_pane(const TabId& tab, const PaneId& pane, std::string command);
    // Clear every pane's command history across the open workspace. Persisted
    // at the next checkpoint (`clear_history()` must survive a restart).
    bool clear_command_history();

    // -- Snapshot --
    SnapshotId create_snapshot();
    bool restore_snapshot(const SnapshotId& snap);

    // Atomic checkpoint: captures runtime state, validates, writes workspace +
    // scrollbacks + snapshot in a single transaction, increments generation.
    // `reason` is one of: "recovery", "autosave", "window_history", "manual".
    // Returns false if no workspace is open or transaction fails.
    bool checkpoint(const std::string& reason);

    // Ingest runtime pane state from the GUI layer (scrollback, cwd, cols/rows,
    // interrupted_command) into the canonical PaneState before checkpoint.
    // Called by SessionController via WorkspaceSnapshotBuilder.
    void apply_runtime_state(const TabId& tab, const PaneId& pane,
                            const remin::core::TerminalRuntimeSnapshot& snap);

    // Mark state dirty (used by autosave scheduler).
    void mark_dirty();

    // Persistence (decision D-2: core mutation only marks dirty; actual
    // writing happens here, called by the autosave policy on flush/shutdown).
    // Returns true if the current workspace existed and was written.
    bool persist();

    // Whether the live workspace has unpersisted changes since the last
    // explicit persist()/save.
    [[nodiscard]] bool dirty() const { return ws_dirty_; }

    // Forcibly persist now and clear the dirty flag (used on shutdown).
    bool persist_now() { return persist(); }

private:
    Storage* storage_;
    std::optional<Workspace> ws_current_;
    bool ws_dirty_ = false;
    WorkspaceEventCallback event_callback_;
    void emit(WorkspaceEvent::Type type, WorkspaceId ws,
              std::optional<WindowId> window = std::nullopt,
              std::optional<TabId> tab = std::nullopt,
              std::optional<PaneId> pane = std::nullopt);
};

} // namespace remin::core
