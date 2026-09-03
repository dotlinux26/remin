#include "core/workspace_lock.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <system_error>

namespace remin::core {

WorkspaceLock::WorkspaceLock() = default;

WorkspaceLock::~WorkspaceLock() {
    release();
}

bool WorkspaceLock::acquire(const std::string& lock_dir, const WorkspaceId& wsid) {
    if (fd_ >= 0) return true;  // already held
    std::error_code ec;
    std::filesystem::create_directories(lock_dir, ec);
    path_ = std::filesystem::path(lock_dir) / (wsid.str() + ".lock");

    int fd = ::open(path_.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return false;
    }
    fd_ = fd;
    return true;
}

bool WorkspaceLock::is_locked(const std::string& lock_dir, const WorkspaceId& wsid) {
    auto path = std::filesystem::path(lock_dir) / (wsid.str() + ".lock");
    int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    bool locked = (::flock(fd, LOCK_EX | LOCK_NB) != 0);
    ::close(fd);
    return locked;
}

void WorkspaceLock::release() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace remin::core
