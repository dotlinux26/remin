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
constexpr const char* kAutoShowPanelKey = "settings:autoshow-panel";
constexpr const char* kPersistOpenWindowsKey = "settings:persist-open-windows";
constexpr const char* kUnsavedCloseKey = "settings:unsaved-close";
constexpr const char* kThemeDarkKey = "settings:theme-dark";
constexpr const char* kColorProfileFgKey = "settings:color-profile-fg";
constexpr const char* kColorProfileBgKey = "settings:color-profile-bg";
constexpr const char* kTerminalFgKey = "settings:terminal-fg";
constexpr const char* kTerminalBgKey = "settings:terminal-bg";
constexpr const char* kWindowHistoryKey = "settings:window-history";
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
        // Wire autosave flush to atomic checkpoint (reason="autosave"). The
        // GUI runtime capture (MainWindow) runs first, so the checkpoint
        // persists live pane/note/directory/focus state, not stale data.
        autosaver_->set_workspace_provider([this]() {
            if (runtime_capture_) runtime_capture_();
            if (core_) core_->checkpoint("autosave");
        });
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
    auto id = core_ ? core_->add_window(title) : remin::core::WindowId{};
    if (!id.empty()) current_window_ = id;
    return id;
}

// Resolve the single core window this MainWindow represents. Order: the window
// pinned by restore_workspace (current_window_) → the workspace's focused
// window → the first window → a freshly created window.
remin::core::WindowId SessionController::ensure_window() {
    if (!core_) return {};
    auto* ws = core_->current_workspace();
    if (!ws) return current_window_;

    const auto find = [&](const remin::core::WindowId& id) -> bool {
        for (const auto& w : ws->windows) if (w.id == id) return true;
        return false;
    };

    if (!current_window_.empty() && find(current_window_)) return current_window_;
    if (ws->focus_window_id && find(*ws->focus_window_id)) {
        current_window_ = *ws->focus_window_id;
        return current_window_;
    }
    if (!ws->windows.empty()) {
        current_window_ = ws->windows.front().id;
        return current_window_;
    }
    current_window_ = core_->add_window(default_window_label(ws));
    return current_window_;
}

// Default user-facing window label ("My Window 1", "My Window 2", …) chosen so
// a fresh window never collides with an existing label in the workspace.
std::string SessionController::default_window_label(const remin::core::Workspace* ws) const {
    int n = 1;
    if (ws) {
        auto taken = [&](const std::string& candidate) {
            for (const auto& w : ws->windows)
                if (w.label == candidate) return true;
            return false;
        };
        while (taken("My Window " + std::to_string(n))) ++n;
    }
    return "My Window " + std::to_string(n);
}

bool SessionController::rename_window(const remin::core::WindowId& id,
                                      const std::string& title) {
    return core_ && core_->rename_window(id, title);
}

SessionController::TerminalTab SessionController::new_terminal_tab(const std::string& title) {
    TerminalTab out;
    if (!core_) return out;
    // V1 GUI is a single MainWindow → one core window. All terminal tabs land
    // in that window (create it on first use); never one window per tab.
    out.window = ensure_window();
    if (out.window.empty()) return out;
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

bool SessionController::focus_pane(const remin::core::TabId& tab,
                                   const remin::core::PaneId& pane) {
    return core_ && core_->focus_pane(tab, pane);
}

bool SessionController::close_tab(const remin::core::WindowId& window,
                                  const remin::core::TabId& tab) {
    // The MainWindow will handle the actual tab closing via its own method
    // This is a placeholder - the actual closing is done by MainWindow
    // which has access to the GUI widgets
    return false; // Will be handled by MainWindow directly
}

std::string SessionController::new_note() {
    if (!core_) return {};
    // Notes share the pane id space (globally unique ids); the body is stored
    // in the generic blob store keyed by this id.
    std::string noteId = remin::core::PaneId::generate().str();
    const auto win = ensure_window();
    if (win.empty()) return {};
    // Register the note as a real tab (kind=Note) so it is restored like every
    // other surface — not just a detached blob.
    remin::core::NoteTabState st;
    st.document_id = noteId;
    st.title = noteId.empty() ? "note" : noteId;
    core_->add_note_tab(win, st.title, st);
    return noteId;
}

std::optional<std::pair<remin::core::WindowId, remin::core::TabId>>
SessionController::note_tab_binding(const std::string& noteId) const {
    if (!core_ || noteId.empty()) return std::nullopt;
    auto* ws = core_->current_workspace();
    if (!ws) return std::nullopt;
    for (const auto& w : ws->windows) {
        for (const auto& t : w.tabs) {
            if (t.kind == remin::core::TabKind::Note && t.note_state &&
                t.note_state->document_id == noteId) {
                return std::make_pair(w.id, t.id);
            }
        }
    }
    return std::nullopt;
}

// Create a note tab from existing NoteTabState (for restore).
std::string SessionController::restore_note(const remin::core::NoteTabState& state) {
    if (!core_) return {};
    // The note body is loaded from storage via load_note().
    // The note id comes from the state's document_id.
    return state.document_id;
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

bool SessionController::auto_show_panel_enabled() const {
    if (!storage_) return false;
    const std::string val = storage_->load_scrollback(meta_id(kAutoShowPanelKey));
    return val == "1";
}

void SessionController::set_auto_show_panel_enabled(bool enabled) {
    if (!storage_) return;
    storage_->store_scrollback(meta_id(kAutoShowPanelKey), enabled ? "1" : "0");
}

bool SessionController::persist_open_windows() const {
    if (!storage_) return true;
    const std::string val = storage_->load_scrollback(meta_id(kPersistOpenWindowsKey));
    // Missing setting → ON (window session persistence is the default).
    return val.empty() || val == "1";
}

void SessionController::set_persist_open_windows(bool enabled) {
    if (!storage_) return;
    storage_->store_scrollback(meta_id(kPersistOpenWindowsKey), enabled ? "1" : "0");
}

SessionController::UnsavedClose SessionController::unsaved_close_behavior() const {
    if (!storage_) return UnsavedClose::Ask;
    const std::string val = storage_->load_scrollback(meta_id(kUnsavedCloseKey));
    if (val == "keep") return UnsavedClose::Keep;
    if (val == "skip") return UnsavedClose::Skip;
    return UnsavedClose::Ask;
}

void SessionController::set_unsaved_close_behavior(UnsavedClose behavior) {
    if (!storage_) return;
    const char* val = behavior == UnsavedClose::Keep  ? "keep"
                      : behavior == UnsavedClose::Skip ? "skip"
                                                        : "ask";
    storage_->store_scrollback(meta_id(kUnsavedCloseKey), val);
}

bool SessionController::window_history_enabled() const {
    if (!storage_) return true; // default ON
    const std::string val = storage_->load_scrollback(meta_id(kWindowHistoryKey));
    return val.empty() || val == "1";
}

void SessionController::set_window_history_enabled(bool enabled) {
    if (!storage_) return;
    storage_->store_scrollback(meta_id(kWindowHistoryKey), enabled ? "1" : "0");
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

std::optional<SessionController::ColorProfile> SessionController::terminal_colors() const {
    if (!storage_) return std::nullopt;
    const std::string fg = storage_->load_scrollback(meta_id(kTerminalFgKey));
    const std::string bg = storage_->load_scrollback(meta_id(kTerminalBgKey));
    if (fg.empty() && bg.empty()) return std::nullopt;
    ColorProfile profile;
    profile.foreground = fg;
    profile.background = bg;
    return profile;
}

void SessionController::set_terminal_colors(const std::string& fg, const std::string& bg) {
    if (!storage_) return;
    storage_->store_scrollback(meta_id(kTerminalFgKey), fg);
    storage_->store_scrollback(meta_id(kTerminalBgKey), bg);
}

bool SessionController::add_command_to_pane(const remin::core::TabId& tab,
                                            const remin::core::PaneId& pane,
                                            const remin::core::CommandRecord& record) {
    return core_ && core_->add_command_to_pane(tab, pane, record);
}

void SessionController::migrate_legacy_command_history() {
    // Design §6.1: the pre-canonical sidebar stored one global, newline-
    // separated list under `settings:command-history`. Migrate it once into the
    // first terminal pane's canonical per-pane history, then drop the old key.
    if (!storage_) return;
    constexpr const char* kHistoryKey = "settings:command-history";
    std::string existing = storage_->load_scrollback(meta_id(kHistoryKey));
    if (existing.empty()) return;

    remin::core::Workspace* ws = core_ ? core_->current_workspace() : nullptr;
    if (!ws) return;
    for (auto& w : ws->windows) {
        for (auto& t : w.tabs) {
            std::vector<remin::core::Pane*> panes;
            t.pane_tree.collect_panes(panes);
            for (auto* p : panes) {
                if (!p) continue;
                // Only seed into a pane with no history; dedupe via the core API.
                if (!p->state.command_history.empty()) continue;
                std::istringstream iss(existing);
                std::string line;
                while (std::getline(iss, line)) {
                    if (!line.empty()) {
                        // Legacy rows carry no timestamp (unknowable now → 0).
                        core_->add_command_to_pane(t.id, p->id,
                                                   remin::core::CommandRecord{line, 0});
                    }
                }
                storage_->store_scrollback(meta_id(kHistoryKey), "");
                return;
            }
        }
    }
}

std::vector<std::string> SessionController::get_command_history() const {
    std::vector<std::string> result;
    if (!core_) return result;
    const remin::core::Workspace* ws = core_->current_workspace();
    if (!ws) return result;
    for (const auto& e : remin::core::aggregate_command_history(*ws)) {
        result.push_back(e.record.command);
    }
    return result;
}

bool SessionController::clear_command_history() {
    return core_ && core_->clear_command_history();
}

// -- Checkpoint / Persistence --

void SessionController::capture_all_runtime_state() {
    // The GUI owns the capture (MainWindow registers it via
    // set_runtime_capture_callback). This wrapper is invoked before every
    // checkpoint; without a registered capture there is nothing to do.
    if (runtime_capture_) runtime_capture_();
}

bool SessionController::checkpoint_recovery() {
    if (!core_) return false;
    // Capture runtime state from all panes before checkpoint
    capture_all_runtime_state();
    return core_->checkpoint("recovery");
}

bool SessionController::checkpoint_manual() {
    if (!core_) return false;
    capture_all_runtime_state();
    return core_->checkpoint("manual");
}

} // namespace remin::gui
