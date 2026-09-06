#include "core/workspace_core.hpp"
#include "core/serialization.hpp"

#include <algorithm>
#include <stdexcept>

namespace remin::core {

WorkspaceCore::WorkspaceCore(Storage* storage) : storage_(storage) {}

WorkspaceCore::~WorkspaceCore() = default;

void WorkspaceCore::emit(WorkspaceEvent::Type type, WorkspaceId ws,
                         std::optional<WindowId> window,
                         std::optional<TabId> tab,
                         std::optional<PaneId> pane) {
    if (event_callback_) {
        event_callback_({type, std::move(ws), std::move(window), std::move(tab), std::move(pane)});
    }
}

// -- Workspaces --

WorkspaceId WorkspaceCore::create_workspace(std::string name) {
    auto ws = Workspace::create(name.empty() ? "untitled" : name);
    const auto id = ws.id;
    storage_->save_workspace(ws);
    ws_current_ = std::move(ws);
    emit(WorkspaceEvent::Type::WorkspaceOpened, ws_current_->id);
    return id;
}

bool WorkspaceCore::open_workspace(const WorkspaceId& id) {
    auto loaded = storage_->load_workspace(id);
    if (!loaded) return false;
    ws_current_ = std::move(*loaded);
    emit(WorkspaceEvent::Type::WorkspaceOpened, ws_current_->id);
    return true;
}

bool WorkspaceCore::close_workspace() {
    if (!ws_current_) return false;
    // Final checkpoint is handled by the autosaver policy (flush_now on close/
    // shutdown) via persist(). We only emit the close event and clear state.
    emit(WorkspaceEvent::Type::WorkspaceClosed, ws_current_->id);
    ws_current_.reset();
    return true;
}

const Workspace* WorkspaceCore::current_workspace() const {
    return ws_current_ ? &*ws_current_ : nullptr;
}

Workspace* WorkspaceCore::current_workspace() {
    return ws_current_ ? &*ws_current_ : nullptr;
}

bool WorkspaceCore::rename_workspace(const WorkspaceId& id, std::string name) {
    if (!ws_current_ || ws_current_->id != id) return false;
    ws_current_->name = std::move(name);
    mark_dirty();
    return true;
}

bool WorkspaceCore::delete_workspace(const WorkspaceId& id) {
    if (ws_current_ && ws_current_->id == id) return false; // must close first
    storage_->delete_workspace(id);
    return true;
}

// -- Windows --

WindowId WorkspaceCore::add_window(std::string title) {
    if (!ws_current_) throw std::runtime_error("no open workspace");
    auto win = Window::create(title.empty() ? "window" : title);
    const auto id = win.id;
    ws_current_->windows.push_back(std::move(win));
    ws_current_->focus_window_id = id;
    mark_dirty();
    emit(WorkspaceEvent::Type::WindowAdded, ws_current_->id, id);
    return id;
}

bool WorkspaceCore::remove_window(const WindowId& id) {
    if (!ws_current_) return false;
    auto& wins = ws_current_->windows;
    wins.erase(std::remove_if(wins.begin(), wins.end(),
                              [&](const Window& w) { return w.id == id; }),
               wins.end());
    if (ws_current_->focus_window_id == id) ws_current_->focus_window_id.reset();
    mark_dirty();
    emit(WorkspaceEvent::Type::WindowRemoved, ws_current_->id, id);
    return true;
}

bool WorkspaceCore::rename_window(const WindowId& id, std::string title) {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        if (w.id == id) {
            w.label = std::move(title);
            mark_dirty();
            emit(WorkspaceEvent::Type::WindowRenamed, ws_current_->id, id);
            return true;
        }
    }
    return false;
}

bool WorkspaceCore::focus_window(const WindowId& id) {
    if (!ws_current_) return false;
    for (const auto& w : ws_current_->windows) {
        if (w.id == id) {
            ws_current_->focus_window_id = id;
            mark_dirty();
            return true;
        }
    }
    return false;
}

// -- Tabs --

TabId WorkspaceCore::add_tab(const WindowId& window, std::string title, PaneTree initial_pane) {
    if (!ws_current_) throw std::runtime_error("no open workspace");
    for (auto& w : ws_current_->windows) {
        if (w.id == window) {
            auto tab = Tab::create(title.empty() ? "tab" : title);
            const auto id = tab.id;
            tab.pane_tree = std::move(initial_pane);
            w.tabs.push_back(std::move(tab));
            w.focus_tab_id = id;
            mark_dirty();
            emit(WorkspaceEvent::Type::TabAdded, ws_current_->id, window, id);
            return id;
        }
    }
    throw std::runtime_error("window not found");
}

TabId WorkspaceCore::add_note_tab(const WindowId& window, std::string title,
                                  NoteTabState state) {
    if (!ws_current_) throw std::runtime_error("no open workspace");
    for (auto& w : ws_current_->windows) {
        if (w.id == window) {
            auto tab = Tab::create(title.empty() ? "note" : title);
            const auto id = tab.id;
            tab.kind = TabKind::Note;
            tab.note_state = std::move(state);
            w.tabs.push_back(std::move(tab));
            w.focus_tab_id = id;
            mark_dirty();
            emit(WorkspaceEvent::Type::TabAdded, ws_current_->id, window, id);
            return id;
        }
    }
    throw std::runtime_error("window not found");
}

bool WorkspaceCore::set_tab_note_state(const WindowId& window, const TabId& tab,
                                       const NoteTabState& state) {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        if (w.id != window) continue;
        for (auto& t : w.tabs) {
            if (t.id != tab) continue;
            if (t.kind != TabKind::Note) return false;
            t.note_state = state;
            mark_dirty();
            return true;
        }
    }
    return false;
}

bool WorkspaceCore::remove_tab(const WindowId& window, const TabId& tab) {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        if (w.id == window) {
            w.tabs.erase(std::remove_if(w.tabs.begin(), w.tabs.end(),
                                        [&](const Tab& t) { return t.id == tab; }),
                         w.tabs.end());
            if (w.focus_tab_id == tab) w.focus_tab_id.reset();
            mark_dirty();
            emit(WorkspaceEvent::Type::TabRemoved, ws_current_->id, window, tab);
            return true;
        }
    }
    return false;
}

bool WorkspaceCore::focus_tab(const WindowId& window, const TabId& tab) {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        if (w.id == window) {
            w.focus_tab_id = tab;
            mark_dirty();
            return true;
        }
    }
    return false;
}

// -- Panes --

namespace {

struct PaneTreeMutation {
    bool found = false;
    bool removed = false;
    PaneId splitting;
    PaneTree::Kind kind{PaneTree::Kind::SplitHorizontal};
    double ratio{0.5};
};

PaneTree* find_pane(PaneTree* tree, const PaneId& id) {
    if (!tree) return nullptr;
    if (tree->kind() == PaneTree::Kind::Pane) {
        return (tree->pane() && tree->pane()->id == id) ? tree : nullptr;
    }
    if (auto* f = find_pane(tree->first(), id)) return f;
    return find_pane(tree->second(), id);
}

// Remove a leaf pane. If a split node ends up with a single surviving child,
// that child replaces the split (so we never keep one-sided splits).
// Returns true if id was found and the tree was modified.
bool remove_pane_from_tree(PaneTree& node, const PaneId& id) {
    if (node.kind() == PaneTree::Kind::Pane) {
        if (node.pane() && node.pane()->id == id) {
            node = PaneTree{};  // empty node; caller will prune
            return true;
        }
        return false;
    }

    bool removed = false;
    if (node.first()) removed |= remove_pane_from_tree(*node.first(), id);
    if (!removed && node.second()) removed |= remove_pane_from_tree(*node.second(), id);
    if (!removed) return false;

    // Prune empty/single-child splits.
    auto child_alive = [](PaneTree* n) -> bool {
        return n && n->kind() != PaneTree::Kind::Pane ? true : (n != nullptr && n->pane().has_value());
    };

    const bool first_alive = child_alive(node.first());
    const bool second_alive = child_alive(node.second());

    if (first_alive && second_alive) return true;  // still a valid split

    // Exactly one child (or node) survives → promote it.
    // Use a temporary to avoid self-referential move (child is part of node).
    if (first_alive && node.first()) {
        PaneTree temp = std::move(*node.first());
        node = std::move(temp);
    } else if (second_alive && node.second()) {
        PaneTree temp = std::move(*node.second());
        node = std::move(temp);
    } else {
        node = PaneTree{};
    }
    return true;
}

// Set the ratio on the split node that is the pane's direct parent.
bool set_split_ratio(PaneTree& node, const PaneId& pane, double ratio) {
    if (node.kind() == PaneTree::Kind::Pane) return false;
    if (node.first() && node.first()->kind() == PaneTree::Kind::Pane &&
        node.first()->pane() && node.first()->pane()->id == pane) {
        node.set_ratio(ratio);
        return true;
    }
    if (node.second() && node.second()->kind() == PaneTree::Kind::Pane &&
        node.second()->pane() && node.second()->pane()->id == pane) {
        node.set_ratio(ratio);
        return true;
    }
    if (node.first() && set_split_ratio(*node.first(), pane, ratio)) return true;
    if (node.second() && set_split_ratio(*node.second(), pane, ratio)) return true;
    return false;
}

} // namespace

PaneId WorkspaceCore::split_pane(const TabId& tab, PaneTree::Kind kind, double ratio) {
    if (!ws_current_) throw std::runtime_error("no open workspace");
    for (auto& w : ws_current_->windows) {
        for (auto& t : w.tabs) {
            if (t.id != tab) continue;

            // New pane inserted as one side of the split.
            auto new_pane = Pane{PaneId::generate(), PaneState{}};
            const auto new_id = new_pane.id;

            // If the current tree is a bare pane, wrap it in a split with the new pane.
            if (t.pane_tree.kind() == PaneTree::Kind::Pane && t.pane_tree.pane()) {
                auto existing_node = PaneTree{};
                existing_node = std::move(t.pane_tree);
                t.pane_tree = PaneTree::split(kind, std::move(existing_node),
                                              PaneTree::leaf(std::move(new_pane)), ratio);
            } else {
                // Split the focused leaf (first pane) by default.
                auto* focus = find_pane(&t.pane_tree, w.focus_pane_id ? *w.focus_pane_id : PaneId{});
                if (!focus) focus = find_pane(&t.pane_tree, t.pane_tree.pane() ? t.pane_tree.pane()->id : PaneId{});
                if (focus && focus->kind() == PaneTree::Kind::Pane && focus->pane()) {
                    auto existing = std::move(*focus);
                    *focus = PaneTree::split(kind, std::move(existing),
                                             PaneTree::leaf(std::move(new_pane)), ratio);
                }
            }
            w.focus_pane_id = new_id;
            mark_dirty();
            emit(WorkspaceEvent::Type::PaneSplit, ws_current_->id, w.id, tab, new_id);
            return new_id;
        }
    }
    throw std::runtime_error("tab not found");
}

bool WorkspaceCore::remove_pane(const TabId& tab, const PaneId& pane) {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        for (auto& t : w.tabs) {
            if (t.id != tab) continue;
            if (!remove_pane_from_tree(t.pane_tree, pane)) return false;
            if (w.focus_pane_id == pane) w.focus_pane_id.reset();
            mark_dirty();
            emit(WorkspaceEvent::Type::PaneRemoved, ws_current_->id, w.id, tab, pane);
            return true;
        }
    }
    return false;
}

bool WorkspaceCore::focus_pane(const TabId& tab, const PaneId& pane) {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        if (w.focus_tab_id != tab) continue;
        for (auto& t : w.tabs) {
            if (t.id == tab && find_pane(&t.pane_tree, pane)) {
                w.focus_pane_id = pane;
                mark_dirty();
                return true;
            }
        }
    }
    return false;
}

bool WorkspaceCore::set_pane_ratio(const TabId& tab, const PaneId& pane, double ratio) {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        for (auto& t : w.tabs) {
            if (t.id != tab) continue;
            if (ratio < 0.0 || ratio > 1.0) return false;
            if (!set_split_ratio(t.pane_tree, pane, ratio)) return false;
            mark_dirty();
            emit(WorkspaceEvent::Type::PaneResized, ws_current_->id, w.id, tab, pane);
            return true;
        }
    }
    return false;
}

// -- Per-pane command history (canonical, design §6) --

bool WorkspaceCore::add_command_to_pane(const TabId& tab, const PaneId& pane,
                                        CommandRecord record) {
    if (!ws_current_ || record.command.empty()) return false;
    for (auto& w : ws_current_->windows) {
        for (auto& t : w.tabs) {
            if (t.id != tab) continue;
            PaneTree* node = find_pane(&t.pane_tree, pane);
            if (!node || !node->pane()) return false;
            auto& hist = node->pane()->state.command_history;
            // Adjacent repeats collapse (pressing Up+Enter re-runs a command).
            if (!hist.empty() && hist.back().command == record.command) return true;
            hist.push_back(std::move(record));
            if (hist.size() > kMaxCommandHistoryPerPane) {
                using Diff = std::vector<CommandRecord>::difference_type;
                const Diff drop = static_cast<Diff>(hist.size() - kMaxCommandHistoryPerPane);
                hist.erase(hist.begin(), hist.begin() + drop);
            }
            mark_dirty();
            return true;
        }
    }
    return false;
}

bool WorkspaceCore::clear_command_history() {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        for (auto& t : w.tabs) {
            std::vector<Pane*> panes;
            t.pane_tree.collect_panes(panes);
            for (auto* p : panes) {
                if (p) p->state.command_history.clear();
            }
        }
    }
    mark_dirty();
    return true;
}

// -- Per-pane runtime state ingestion --

void WorkspaceCore::apply_runtime_state(const TabId& tab, const PaneId& pane,
                                       const remin::core::TerminalRuntimeSnapshot& snap) {
    if (!ws_current_) return;
    for (auto& w : ws_current_->windows) {
        for (auto& t : w.tabs) {
            if (t.id != tab) continue;
            PaneTree* node = find_pane(&t.pane_tree, pane);
            if (!node || !node->pane()) return;
            auto& st = node->pane()->state;
            st.cwd = snap.cwd;
            st.shell = snap.shell;
            st.cols = snap.cols;
            st.rows = snap.rows;
            st.scrollback = snap.scrollback;
            st.interrupted_command = snap.interrupted_command;
            // command_history is already canonical (stored in core via add_command_to_pane)
            // but the runtime snapshot may carry a fresh copy; overlay it:
            if (!snap.command_history.empty()) st.command_history = snap.command_history;
            mark_dirty();
            return;
        }
    }
}

// -- Checkpoint (atomic persistence) --

bool WorkspaceCore::checkpoint(const std::string& reason) {
    if (!ws_current_) return false;
    if (!storage_) return false;

    // Update workspace metadata
    ws_current_->last_saved_at = std::chrono::system_clock::now();
    ws_current_->generation++;

    // Serialize workspace to JSON
    remin::core::json ws_json;
    to_json(ws_json, *ws_current_);

    // Collect all pane scrollbacks from current workspace
    std::vector<std::pair<PaneId, std::string>> scrollbacks;
    for (const auto& w : ws_current_->windows) {
        for (const auto& t : w.tabs) {
            std::vector<const Pane*> panes;
            t.pane_tree.collect_panes(panes);
            for (const auto* p : panes) {
                if (p && !p->state.scrollback.empty()) {
                    scrollbacks.emplace_back(p->id, p->state.scrollback);
                }
            }
        }
    }

    // Single atomic write via Storage::checkpoint
    const int schema_version = ws_current_->schema_version;
    const int64_t generation = ws_current_->generation;
    bool ok = storage_->checkpoint(ws_current_->id, ws_json, schema_version,
                                   generation, reason, scrollbacks);
    if (ok) {
        ws_dirty_ = false;
        // Add snapshot id to workspace's snapshot list
        // Note: snapshot id was generated inside storage->checkpoint; we don't have it back.
        // For now, we don't track it in workspace; can be extended later.
    }
    return ok;
}

// -- Snapshot --

SnapshotId WorkspaceCore::create_snapshot() {
    if (!ws_current_) throw std::runtime_error("no open workspace");
    Snapshot snap;
    snap.id = SnapshotId::generate();
    snap.timestamp = std::chrono::system_clock::now();
    snap.revision = static_cast<int>(storage_->list_snapshots(ws_current_->id).size()) + 1;

    json state;
    to_json(state, *ws_current_);
    const auto ser = state.dump();
    snap.size_bytes = ser.size();
    storage_->save_snapshot(ws_current_->id, snap, state);

    ws_current_->snapshot_ids.push_back(snap.id);
    mark_dirty();
    emit(WorkspaceEvent::Type::SnapshotCreated, ws_current_->id, std::nullopt, std::nullopt, std::nullopt);
    return snap.id;
}

bool WorkspaceCore::restore_snapshot(const SnapshotId& snap) {
    if (!ws_current_) return false;
    auto state = storage_->load_snapshot(ws_current_->id, snap);
    if (!state) return false;
    Workspace restored;
    from_json(*state, restored);
    ws_current_ = std::move(restored);
    return true;
}

void WorkspaceCore::mark_dirty() {
    ws_dirty_ = true;
    if (ws_current_) {
        emit(WorkspaceEvent::Type::StateDirty, ws_current_->id);
    }
}

bool WorkspaceCore::persist() {
    if (!ws_current_) return false;
    if (ws_dirty_) {
        storage_->save_workspace(*ws_current_);
        ws_dirty_ = false;
    }
    return true;
}

} // namespace remin::core
