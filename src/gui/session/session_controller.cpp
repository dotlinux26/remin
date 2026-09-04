#include "gui/session/session_controller.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace remin::gui {

namespace {

constexpr const char* kTempPrefix = "remin-note-";
constexpr const char* kPathPrefix = "note-path:";
constexpr const char* kAutosaveTempKey = "settings:autosave-temp";
constexpr const char* kAutoReloadKey = "settings:autoreload";
constexpr const char* kThemeDarkKey = "settings:theme-dark";
constexpr const char* kColorProfileFgKey = "settings:color-profile-fg";
constexpr const char* kColorProfileBgKey = "settings:color-profile-bg";
// Reserved, prefixed ids in the shared blob store must be mutated safely; the
// blob interface is keyed by PaneId, so we wrap metadata strings in PaneId.
remin::core::PaneId meta_id(const std::string& key) {
    return remin::core::PaneId{key};
}

} // namespace

SessionController::SessionController(remin::core::WorkspaceCore* core,
                                     remin::core::Storage* storage,
                                     remin::core::Autosaver* autosaver)
    : core_(core), storage_(storage), autosaver_(autosaver) {
    // One autosave system; per-resource policy configured at the session level.
    if (autosaver_) {
        autosaver_->set_terminal_debounce(std::chrono::seconds(2));
        autosaver_->set_note_idle(std::chrono::seconds(10));
    }
}

bool SessionController::open_workspace(const remin::core::WorkspaceId& id) {
    return core_ && core_->open_workspace(id);
}

bool SessionController::close_workspace() {
    return core_ && core_->close_workspace();
}

bool SessionController::rename_workspace(const std::string& name) {
    if (!core_ || !core_->current_workspace()) return false;
    return core_->rename_workspace(core_->current_workspace()->id, name);
}

remin::core::WindowId SessionController::add_window(const std::string& title) {
    return core_ ? core_->add_window(title) : remin::core::WindowId{};
}

bool SessionController::rename_window(const remin::core::WindowId& id,
                                      const std::string& title) {
    return core_ && core_->rename_window(id, title);
}

SessionController::TerminalTab SessionController::new_terminal_tab(const std::string& title) {
    TerminalTab out;
    if (!core_) return out;
    out.window = core_->add_window("Window");
    auto leaf = remin::core::PaneTree::leaf(
        remin::core::Pane{remin::core::PaneId::generate(), remin::core::PaneState{}});
    out.tab = core_->add_tab(out.window, title, std::move(leaf));
    if (const auto* w = core_->current_workspace()) {
        for (const auto& win : w->windows) {
            if (win.id == out.window) {
                for (const auto& t : win.tabs) {
                    if (t.id == out.tab && t.pane_tree.pane()) {
                        out.root_pane = t.pane_tree.pane()->id;
                    }
                }
            }
        }
    }
    return out;
}

remin::core::PaneId SessionController::split_pane(const remin::core::TabId& tab,
                                                  remin::core::PaneTree::Kind kind,
                                                  double ratio) {
    return core_ ? core_->split_pane(tab, kind, ratio) : remin::core::PaneId{};
}

bool SessionController::remove_pane(const remin::core::TabId& tab,
                                    const remin::core::PaneId& pane) {
    return core_ && core_->remove_pane(tab, pane);
}

bool SessionController::set_pane_ratio(const remin::core::TabId& tab,
                                       const remin::core::PaneId& pane,
                                       double ratio) {
    return core_ && core_->set_pane_ratio(tab, pane, ratio);
}

std::string SessionController::new_note() {
    if (!core_) return {};
    // Notes share the pane id space (globally unique ids); the body is stored
    // in the generic blob store keyed by this id.
    return remin::core::PaneId::generate().str();
}

std::string SessionController::load_note(const std::string& noteId) {
    if (!storage_) return {};
    return storage_->load_scrollback(remin::core::PaneId(noteId));
}

remin::core::SnapshotId SessionController::create_snapshot() {
    return core_ ? core_->create_snapshot() : remin::core::SnapshotId{};
}

bool SessionController::restore_snapshot(const remin::core::SnapshotId& snap) {
    return core_ && core_->restore_snapshot(snap);
}

std::string SessionController::note_path(const std::string& noteId) const {
    if (!storage_) return {};
    return storage_->load_scrollback(meta_id(std::string(kPathPrefix) + noteId));
}

void SessionController::set_note_path(const std::string& noteId,
                                      const std::string& path) {
    if (!storage_) return;
    storage_->store_scrollback(meta_id(std::string(kPathPrefix) + noteId), path);
}

bool SessionController::write_note_file(const std::string& path,
                                        const std::string& content) {
    try {
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path());
        std::ofstream out(path, std::ios::trunc);
        if (!out) return false;
        out << content;
        return true;
    } catch (...) {
        return false;
    }
}

std::string SessionController::note_temp_path(const std::string& noteId) const {
    const char* tmp = std::getenv("XDG_RUNTIME_DIR");
    const char* home = std::getenv("HOME");
    std::string dir;
    if (tmp && *tmp) {
        dir = std::string(tmp) + "/remin";
    } else if (home && *home) {
        dir = std::string(home) + "/.local/share/remin/tmp";
    } else {
        dir = "/tmp/remin";
    }
    try {
        std::filesystem::create_directories(dir);
    } catch (...) {}
    return dir + "/" + kTempPrefix + noteId + ".md";
}

bool SessionController::autosave_temp_enabled() const {
    if (!storage_) return true; // default on
    const std::string val = storage_->load_scrollback(meta_id(kAutosaveTempKey));
    return val == "1";
}

void SessionController::set_autosave_temp_enabled(bool enabled) {
    if (!storage_) return;
    storage_->store_scrollback(meta_id(kAutosaveTempKey), enabled ? "1" : "0");
}

bool SessionController::auto_reload_enabled() const {
    if (!storage_) return false;
    const std::string val = storage_->load_scrollback(meta_id(kAutoReloadKey));
    return val == "1";
}

void SessionController::set_auto_reload_enabled(bool enabled) {
    if (!storage_) return;
    storage_->store_scrollback(meta_id(kAutoReloadKey), enabled ? "1" : "0");
}

bool SessionController::theme_dark() const {
    if (!storage_) return false;
    const std::string val = storage_->load_scrollback(meta_id(kThemeDarkKey));
    return val == "1";
}

void SessionController::set_theme_dark(bool dark) {
    if (!storage_) return;
    storage_->store_scrollback(meta_id(kThemeDarkKey), dark ? "1" : "0");
}

std::optional<SessionController::ColorProfile> SessionController::color_profile() const {
    if (!storage_) return std::nullopt;
    const std::string fg = storage_->load_scrollback(meta_id(kColorProfileFgKey));
    const std::string bg = storage_->load_scrollback(meta_id(kColorProfileBgKey));
    if (fg.empty() && bg.empty()) return std::nullopt;
    ColorProfile profile;
    profile.foreground = fg;
    profile.background = bg;
    return profile;
}

void SessionController::set_color_profile(const ColorProfile& profile) {
    if (!storage_) return;
    storage_->store_scrollback(meta_id(kColorProfileFgKey), profile.foreground);
    storage_->store_scrollback(meta_id(kColorProfileBgKey), profile.background);
}

void SessionController::add_command_history(const std::string& command) {
    if (!storage_ || command.empty()) return;
    // Load existing history, append, save back (max 2000 entries)
    constexpr const char* kHistoryKey = "settings:command-history";
    std::string existing = storage_->load_scrollback(meta_id(kHistoryKey));
    std::vector<std::string> history;
    if (!existing.empty()) {
        // Parse newline-separated history
        std::istringstream iss(existing);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) history.push_back(line);
        }
    }
    // Avoid exact duplicates of the last entry
    if (history.empty() || history.back() != command) {
        history.push_back(command);
    }
    // Trim to 2000
    if (history.size() > 2000) {
        history.erase(history.begin(), history.begin() + (history.size() - 1000));
    }
    // Serialize back
    std::string serialized;
    for (const auto& h : history) {
        serialized += h;
        serialized += '\n';
    }
    storage_->store_scrollback(meta_id(kHistoryKey), serialized);
}

std::vector<std::string> SessionController::get_command_history() const {
    std::vector<std::string> result;
    if (!storage_) return result;
    constexpr const char* kHistoryKey = "settings:command-history";
    std::string existing = storage_->load_scrollback(meta_id(kHistoryKey));
    if (existing.empty()) return result;
    std::istringstream iss(existing);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) result.push_back(line);
    }
    return result;
}

} // namespace remin::gui
