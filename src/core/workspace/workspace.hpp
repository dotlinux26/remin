#pragma once

#include "core/id.hpp"
#include "core/window/window.hpp"

#include <chrono>
#include <string>
#include <vector>
#include <optional>

namespace remin::core {

// A workspace: the top-level unit. Contains windows, snapshots, history.
struct Workspace {
    WorkspaceId id;
    std::string name;
    std::string working_directory;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_saved_at;
    std::vector<std::string> tags;

    std::vector<Window> windows;
    std::optional<WindowId> focus_window_id;
    std::vector<SnapshotId> snapshot_ids;

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

} // namespace remin::core
