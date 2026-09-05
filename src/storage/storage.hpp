#pragma once

#include "core/workspace_core.hpp"
#include "storage/sqlite/sqlite_db.hpp"

namespace remin::storage {

// SQLite-backed implementation of remin::core::Storage.
// Canonical persistence for workspaces + snapshots + scrollback blobs.
class SqliteStorage : public remin::core::Storage {
public:
    explicit SqliteStorage(std::string db_path);
    ~SqliteStorage() override;

    // remin::core::Storage
    std::vector<remin::core::Workspace> list_workspaces() override;
    std::optional<remin::core::Workspace> load_workspace(const remin::core::WorkspaceId& id) override;
    void save_workspace(const remin::core::Workspace& workspace) override;
    void delete_workspace(const remin::core::WorkspaceId& id) override;

    std::vector<remin::core::Snapshot> list_snapshots(const remin::core::WorkspaceId& id) override;
    std::optional<remin::core::json> load_snapshot(const remin::core::WorkspaceId& id,
                                                   const remin::core::SnapshotId& snap) override;
    void save_snapshot(const remin::core::WorkspaceId& id, const remin::core::Snapshot& snap,
                       const remin::core::json& state) override;
    void delete_snapshot(const remin::core::WorkspaceId& id, const remin::core::SnapshotId& snap) override;

    void store_scrollback(const remin::core::PaneId& pane, std::string content) override;
    std::string load_scrollback(const remin::core::PaneId& pane) override;

    // Atomic checkpoint: workspace + scrollbacks + snapshot in one transaction.
    bool checkpoint(const remin::core::WorkspaceId& ws_id,
                    const remin::core::json& workspace_state,
                    int schema_version,
                    int64_t generation,
                    const std::string& reason,
                    const std::vector<std::pair<remin::core::PaneId, std::string>>& scrollbacks);

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::string error() const { return err_; }

private:
    std::unique_ptr<SqliteDb> db_;
    bool ok_{false};
    std::string err_;
};

} // namespace remin::storage
