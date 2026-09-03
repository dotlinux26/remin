#pragma once

#include "core/autosave.hpp"
#include "core/workspace_core.hpp"
#include "core/workspace_lock.hpp"
#include "gui/session/session_controller.hpp"
#include "storage/storage.hpp"

#include <memory>
#include <string>

namespace remin::gui {

// Owning session for the GUI process (the single authority, no daemon).
// Owns the SQLite storage, the WorkspaceCore, the workspace lock, the unified
// autosaver, and the SessionController that orchestrates UI commands.
//
// The session:
//   - computes the XDG data dir, creates it if needed
//   - creates or reopens a default workspace
//   - takes an advisory lock so a second GUI can't edit the same workspace
//   - exposes the controller (and low-level core/autosaver) to the shell
class WorkspaceSession {
public:
    WorkspaceSession();
    ~WorkspaceSession();

    WorkspaceSession(const WorkspaceSession&) = delete;
    WorkspaceSession& operator=(const WorkspaceSession&) = delete;

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::string error() const { return err_; }

    [[nodiscard]] SessionController* controller() { return controller_.get(); }
    [[nodiscard]] remin::core::WorkspaceCore* core() { return core_.get(); }
    [[nodiscard]] remin::core::Autosaver* autosaver() { return autosaver_.get(); }
    [[nodiscard]] const std::string& lock_dir() const { return lock_dir_; }

    // Standard XDG data dir (shared with the CLI).
    static std::string data_dir();

private:
    bool ok_{false};
    std::string err_;
    std::string lock_dir_;
    std::unique_ptr<remin::storage::SqliteStorage> storage_;
    std::unique_ptr<remin::core::WorkspaceCore> core_;
    std::unique_ptr<remin::core::Autosaver> autosaver_;
    std::unique_ptr<remin::core::WorkspaceLock> lock_;
    std::unique_ptr<SessionController> controller_;
};

} // namespace remin::gui
