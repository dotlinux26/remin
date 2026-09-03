#pragma once

#include "core/id.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace remin::core {

// Advisory exclusive lock for a workspace, backed by flock(2) on a lock file.
//
// The GUI process is the authority (no daemon, ADR-0006). Opening a workspace
// in the GUI takes this lock; a second GUI (or a mutating CLI session) is
// refused while the first is alive. The OS releases the lock when the holding
// process exits, so stale locks self-heal.
class WorkspaceLock {
public:
    WorkspaceLock();
    ~WorkspaceLock();
    WorkspaceLock(const WorkspaceLock&) = delete;
    WorkspaceLock& operator=(const WorkspaceLock&) = delete;

    // Acquire the lock for `wsid` in `lock_dir` (created as needed).
    // Returns true if acquired (or already held by us).
    bool acquire(const std::string& lock_dir, const WorkspaceId& wsid);

    // True while we hold the lock.
    [[nodiscard]] bool held() const noexcept { return fd_ >= 0; }

    // Second instance holding it? (attempt to acquire without blocking).
    static bool is_locked(const std::string& lock_dir, const WorkspaceId& wsid);

    void release();

private:
    int fd_{-1};
    std::filesystem::path path_;
};

} // namespace remin::core
