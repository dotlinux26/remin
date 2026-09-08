#pragma once

#include "core/pane/pane.hpp"

#include <string>
#include <vector>
#include <filesystem>
#include <mutex>
#include <optional>
#include <functional>

namespace remin::gui {

class PaneHistoryTracker {
public:
    // `pane_id` is used only for logging/debugging; the authoritative source is `histfile_path`.
    explicit PaneHistoryTracker(const std::string& histfile_path, const std::string& pane_id = "");

    // Load entire history file and build initial in-memory model.
    void initial_load();

    // Synchronize with the history file: read new/changed content and update the model.
    // Safe to call frequently (e.g., on pane focus, on commit signal, timer).
    // Returns true if new entries were added.
    bool sync();

    // Get all commands, newest first (for Commands panel).
    [[nodiscard]] std::vector<remin::core::CommandRecord> commands_desc() const;

    // Get all commands, oldest first (chronological).
    [[nodiscard]] std::vector<remin::core::CommandRecord> commands_asc() const;

    // Pin/unpin a command by its exact text.
    // If multiple entries match, only the most recent one is pinned.
    void pin_command(const std::string& command, bool pinned);

    // Check if a command is pinned (by exact text match).
    [[nodiscard]] bool is_pinned(const std::string& command) const;

    // Get the HISTFILE path.
    [[nodiscard]] const std::string& histfile_path() const noexcept { return histfile_path_; }

    // Get the pane ID (for debugging).
    [[nodiscard]] const std::string& pane_id() const noexcept { return pane_id_; }

    // Set a callback to be notified when history changes after sync().
    using HistoryChangedCallback = std::function<void()>;
    void set_history_changed_callback(HistoryChangedCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        on_history_changed_ = std::move(cb);
    }

private:
    struct CachedEntry {
        remin::core::CommandRecord record;
        bool pinned = false;
    };

    void parse_and_merge(const std::string& new_content);
    static std::string read_file(const std::string& path);
    static std::uint64_t file_size(const std::string& path);
    static std::uint64_t file_mtime(const std::string& path);

    std::string histfile_path_;
    std::string pane_id_;
    std::vector<CachedEntry> cache_;        // chronological order (oldest first)
    std::uint64_t last_known_size_ = 0;
    std::uint64_t last_known_mtime_ = 0;
    mutable std::mutex mutex_;
    bool initial_load_done_ = false;
    HistoryChangedCallback on_history_changed_;
};

} // namespace remin::gui