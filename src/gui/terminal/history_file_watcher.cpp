#include "gui/terminal/history_file_watcher.hpp"

#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace remin::gui {

HistoryFileWatcher::~HistoryFileWatcher() {
    stop();
}

bool HistoryFileWatcher::start(const std::string& history_dir, ChangeCallback callback) {
    if (running_.load()) return true;

    history_dir_ = history_dir;
    callback_ = std::move(callback);

    // Create inotify instance
    inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ < 0) {
        return false;
    }

    // Ensure directory exists
    std::filesystem::create_directories(history_dir_);

    // Watch the directory for relevant events
    watch_fd_ = inotify_add_watch(inotify_fd_, history_dir_.c_str(),
                                   IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO |
                                   IN_CREATE | IN_DELETE_SELF | IN_MOVE_SELF |
                                   IN_DELETE);
    if (watch_fd_ < 0) {
        close(inotify_fd_);
        inotify_fd_ = -1;
        return false;
    }

    running_.store(true);
    worker_thread_ = std::thread(&HistoryFileWatcher::run, this);
    return true;
}

void HistoryFileWatcher::stop() {
    if (!running_.load()) return;

    running_.store(false);

    if (watch_fd_ >= 0 && inotify_fd_ >= 0) {
        inotify_rm_watch(inotify_fd_, watch_fd_);
        watch_fd_ = -1;
    }

    if (inotify_fd_ >= 0) {
        close(inotify_fd_);
        inotify_fd_ = -1;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void HistoryFileWatcher::register_pane(const std::string& pane_id, const std::string& histfile_path) {
    std::lock_guard<std::mutex> lock(map_mutex_);
    // Extract just the filename for matching
    std::filesystem::path p(histfile_path);
    std::string filename = p.filename().string();
    pane_to_file_[pane_id] = filename;
    file_to_pane_[filename] = pane_id;
}

void HistoryFileWatcher::unregister_pane(const std::string& pane_id) {
    std::lock_guard<std::mutex> lock(map_mutex_);
    auto it = pane_to_file_.find(pane_id);
    if (it != pane_to_file_.end()) {
        file_to_pane_.erase(it->second);
        pane_to_file_.erase(it);
    }
}

bool HistoryFileWatcher::is_running() const {
    return running_.load();
}

void HistoryFileWatcher::run() {
    constexpr size_t kEventBufferSize = 4096;
    char buffer[kEventBufferSize];

    while (running_.load()) {
        // Wait for events with a small timeout to allow clean shutdown
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(inotify_fd_, &fds);
        struct timeval tv = {0, 100000};  // 100ms
        int ret = select(inotify_fd_ + 1, &fds, nullptr, nullptr, &tv);
        
        if (!running_.load()) break;
        if (ret <= 0) continue;

        ssize_t len = read(inotify_fd_, buffer, sizeof(buffer));
        if (len <= 0) continue;

        size_t i = 0;
        while (i < static_cast<size_t>(len)) {
            auto* event = reinterpret_cast<inotify_event*>(buffer + i);
            
            if (event->len > 0) {
                std::string filename(event->name);
                
                // Only care about pane-*.hist files
                if (filename.rfind("pane-", 0) == 0 && 
                    filename.size() > 5 && 
                    filename.rfind(".hist") == filename.size() - 5) {
                    handle_event(filename, event->mask);
                }
            }
            
            i += sizeof(inotify_event) + event->len;
        }
    }
}

void HistoryFileWatcher::handle_event(const std::string& filename, uint32_t mask) {
    // Filter for events that indicate the file content may have changed
    constexpr uint32_t kInterestingMask = IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE;
    if ((mask & kInterestingMask) == 0) return;

    std::string pane_id;
    {
        std::lock_guard<std::mutex> lock(map_mutex_);
        auto it = file_to_pane_.find(filename);
        if (it != file_to_pane_.end()) {
            pane_id = it->second;
        }
    }

    if (!pane_id.empty() && callback_) {
        callback_(pane_id);
    }
}

} // namespace remin::gui