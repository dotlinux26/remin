#include "gui/terminal/pane_history_tracker.hpp"

#include "core/terminal/history_parser.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <mutex>
#include <glibmm.h>

namespace remin::gui {

PaneHistoryTracker::PaneHistoryTracker(const std::string& histfile_path, const std::string& pane_id)
    : histfile_path_(histfile_path), pane_id_(pane_id) {}

void PaneHistoryTracker::initial_load() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Allow forced reload by setting initial_load_done_ = false before calling
    // But we still need to handle the case where it's called the first time
    
    std::string content = read_file(histfile_path_);
    cache_.clear();
    if (!content.empty()) {
        auto recs = remin::core::terminal::parse_history_buffer(content);
        cache_.reserve(recs.size());
        for (auto& r : recs) {
            cache_.push_back({std::move(r), false});
        }
    }
    last_known_size_ = file_size(histfile_path_);
    last_known_mtime_ = file_mtime(histfile_path_);
    initial_load_done_ = true;
    
    // Notify after initial load - defer to avoid deadlock during construction
    if (on_history_changed_) {
        Glib::signal_idle().connect_once([this]() {
            if (on_history_changed_) on_history_changed_();
        });
    }
}

bool PaneHistoryTracker::sync() {
    // Check if file changed without holding the lock for the entire operation
    std::uint64_t current_size = file_size(histfile_path_);
    std::uint64_t current_mtime = file_mtime(histfile_path_);

    bool needs_full_reload = false;
    bool needs_incremental = false;
    std::string new_content;
    std::size_t before_count = 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!initial_load_done_) {
            initial_load();
            return false;
        }

        // If file hasn't changed, nothing to do
        if (current_size == last_known_size_ && current_mtime == last_known_mtime_) {
            return false;
        }

        before_count = cache_.size();

        // If file was truncated (size decreased), do full reload
        if (current_size < last_known_size_) {
            needs_full_reload = true;
        } else if (current_size > last_known_size_) {
            // File grew - read only the new portion
            std::string content = read_file(histfile_path_);
            if (content.size() > last_known_size_) {
                new_content = content.substr(last_known_size_);
                needs_incremental = true;
            } else {
                // Size check inconsistent, do full reload
                needs_full_reload = true;
            }
        }
    } // Lock released here

    if (needs_full_reload) {
        initial_load(); // This will take its own lock
        return cache_.size() != before_count;
    }

    if (needs_incremental && !new_content.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        parse_and_merge(new_content);
        last_known_size_ = current_size;
        last_known_mtime_ = current_mtime;
        return cache_.size() != before_count;
    }

    return false;
}

std::vector<remin::core::CommandRecord> PaneHistoryTracker::commands_desc() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<remin::core::CommandRecord> result;
    result.reserve(cache_.size());
    // Reverse: newest first
    for (auto it = cache_.rbegin(); it != cache_.rend(); ++it) {
        result.push_back(it->record);
        // Preserve pinned state in the returned records
        if (it->pinned) {
            result.back().pinned = true;
        }
    }
    return result;
}

std::vector<remin::core::CommandRecord> PaneHistoryTracker::commands_asc() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<remin::core::CommandRecord> result;
    result.reserve(cache_.size());
    for (const auto& e : cache_) {
        result.push_back(e.record);
        if (e.pinned) {
            result.back().pinned = true;
        }
    }
    return result;
}

void PaneHistoryTracker::pin_command(const std::string& command, bool pinned) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (command.empty()) return;
    // Search from newest (end) to oldest
    for (auto it = cache_.rbegin(); it != cache_.rend(); ++it) {
        if (it->record.command == command) {
            it->pinned = pinned;
            return;
        }
    }
}

bool PaneHistoryTracker::is_pinned(const std::string& command) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (command.empty()) return false;
    for (auto it = cache_.rbegin(); it != cache_.rend(); ++it) {
        if (it->record.command == command) {
            return it->pinned;
        }
    }
    return false;
}

void PaneHistoryTracker::apply_annotations(const std::vector<remin::core::Storage::HistoryAnnotation>& annotations) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& ann : annotations) {
        if (!ann.pinned) continue;
        // Match by command text (most recent first)
        for (auto it = cache_.rbegin(); it != cache_.rend(); ++it) {
            if (it->record.command == ann.command) {
                it->pinned = true;
                break;
            }
        }
    }
}


void PaneHistoryTracker::parse_and_merge(const std::string& new_content) {
    if (new_content.empty()) return;

    auto new_recs = remin::core::terminal::parse_history_buffer(new_content);
    if (new_recs.empty()) return;

    // Simple append with adjacent dedupe (matching core behavior)
    for (auto& r : new_recs) {
        if (!cache_.empty() && cache_.back().record.command == r.command) {
            // Adjacent duplicate - skip, but preserve pin if the new one was pinned
            if (r.pinned) cache_.back().pinned = true;
            continue;
        }
        cache_.push_back({std::move(r), false});
    }
}

std::string PaneHistoryTracker::read_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return {};
    std::string content;
    ifs.seekg(0, std::ios::end);
    content.reserve(static_cast<std::size_t>(ifs.tellg()));
    ifs.seekg(0, std::ios::beg);
    content.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return content;
}

std::uint64_t PaneHistoryTracker::file_size(const std::string& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<std::uint64_t>(sz);
}

std::uint64_t PaneHistoryTracker::file_mtime(const std::string& path) {
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    // Convert to nanoseconds since epoch for comparison
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            ftime.time_since_epoch()).count()
    );
}

} // namespace remin::gui