#include "core/autosave.hpp"
#include "core/workspace_core.hpp"

#include <vector>

namespace remin::core {

namespace {
const char* kWorkspaceKey = "__remin_workspace__";
} // namespace

Autosaver::Autosaver(Storage* storage)
    : storage_(storage) {}

Autosaver::~Autosaver() = default;

void Autosaver::note_terminal_activity(const PaneId& pane) {
    const std::string key = pane.str();
    pending_[key].kind = Kind::Terminal;
    pending_[key].last = now_();
}

void Autosaver::note_note_activity(const std::string& noteId) {
    pending_[noteId].kind = Kind::Note;
    pending_[noteId].last = now_();
}

void Autosaver::note_workspace_activity() {
    pending_[kWorkspaceKey].kind = Kind::Workspace;
    pending_[kWorkspaceKey].last = now_();
}

bool Autosaver::due_entry(const Entry& e) const {
    auto threshold = (e.kind == Kind::Note) ? note_idle_ : terminal_debounce_;
    if (threshold.count() <= 0) return false;
    return (now_() - e.last) >= threshold;
}

bool Autosaver::due() const {
    if (pending_.empty()) return false;
    for (const auto& kv : pending_) {
        if (due_entry(kv.second)) return true;
    }
    return false;
}

bool Autosaver::write_entry(const std::string& id, const Entry& e) {
    switch (e.kind) {
        case Kind::Terminal:
        case Kind::Note:
            return true;
        case Kind::Workspace:
            return true;
    }
    return false;
}

bool Autosaver::flush() {
    bool wrote = false;
    std::vector<std::string> done;
    done.reserve(pending_.size());
    for (auto& kv : pending_) {
        if (due_entry(kv.second)) {
            if (write_entry(kv.first, kv.second)) wrote = true;
            done.push_back(kv.first);
        }
    }
    for (const auto& k : done) pending_.erase(k);
    return wrote;
}

bool Autosaver::flush_now() {
    bool wrote = false;
    if (workspace_provider_) {
        workspace_provider_();
        wrote = true;
    }
    pending_.clear();
    return wrote;
}

} // namespace remin::core