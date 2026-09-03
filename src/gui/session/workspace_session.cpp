#include "gui/session/workspace_session.hpp"

#include <cstdlib>
#include <sys/stat.h>

namespace remin::gui {

std::string WorkspaceSession::data_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::string base = (xdg && *xdg) ? xdg : (home ? std::string(home) + "/.local/share" : "/tmp");
    return base + "/remin";
}

static void mkdir_p(const std::string& path) {
    std::string cur;
    for (auto it = path.begin(); it != path.end(); ++it) {
        cur += *it;
        if (*it == '/' || it + 1 == path.end()) {
            ::mkdir(cur.c_str(), 0755);
        }
    }
}

WorkspaceSession::WorkspaceSession() {
    const auto dir = data_dir();
    mkdir_p(dir);
    lock_dir_ = dir + "/locks";

    storage_ = std::make_unique<remin::storage::SqliteStorage>(dir + "/remin.db");
    if (!storage_->ok()) {
        err_ = "cannot open database: " + storage_->error();
        return;
    }
    core_ = std::make_unique<remin::core::WorkspaceCore>(storage_.get());
    autosaver_ = std::make_unique<remin::core::Autosaver>(storage_.get());
    controller_ = std::make_unique<SessionController>(core_.get(), storage_.get(), autosaver_.get());

    // Open or create a default workspace.
    auto ws_list = storage_->list_workspaces();
    if (ws_list.empty()) {
        const auto id = core_->create_workspace("default");
        if (id.empty()) {
            err_ = "failed to create a workspace";
            return;
        }
    } else {
        if (!core_->open_workspace(ws_list.front().id)) {
            err_ = "failed to open workspace";
            return;
        }
    }

    // Take the advisory lock; refuse to run a second instance on the same ws.
    lock_ = std::make_unique<remin::core::WorkspaceLock>();
    if (!lock_->acquire(lock_dir_, core_->current_workspace()->id)) {
        err_ = "workspace is already open in another Remin instance";
        return;
    }

    ok_ = true;
}

WorkspaceSession::~WorkspaceSession() = default;

} // namespace remin::gui
