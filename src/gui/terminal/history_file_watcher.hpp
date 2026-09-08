#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

namespace remin::gui {

class PaneHistoryTracker;

/// Filesystem watcher for the history directory using inotify.
/// Watches ~/.local/share/remin/history/ for changes to pane-*.hist files.
class HistoryFileWatcher {
public:
    using ChangeCallback = std::function<void(const std::string& pane_id)>;

    HistoryFileWatcher() = default;
    ~HistoryFileWatcher();

    // Start watching the given directory. Returns true on success.
    bool start(const std::string& history_dir, ChangeCallback callback);

    // Stop watching.
    void stop();

    // Register a pane ID to its history file path for mapping events.
    void register_pane(const std::string& pane_id, const std::string& histfile_path);

    // Unregister a pane.
    void unregister_pane(const std::string& pane_id);

    // Check if watcher is running.
    [[nodiscard]] bool is_running() const;

private:
    // Process inotify events in a loop (runs in background thread).
    void run();

    // Handle a single inotify event.
    void handle_event(const std::string& filename, uint32_t mask);

    std::string history_dir_;
    int inotify_fd_ = -1;
    int watch_fd_ = -1;
    ChangeCallback callback_;
    std::unordered_map<std::string, std::string> pane_to_file_;  // pane_id -> histfile_path
    std::unordered_map<std::string, std::string> file_to_pane_;  // histfile_path -> pane_id
    std::mutex map_mutex_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
};

} // namespace remin::gui