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
    storage_->save_workspace(*ws_current_);
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
    storage_->save_workspace(*ws_current_);
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
    storage_->save_workspace(*ws_current_);
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
    storage_->save_workspace(*ws_current_);
    emit(WorkspaceEvent::Type::WindowRemoved, ws_current_->id, id);
    return true;
}

bool WorkspaceCore::rename_window(const WindowId& id, std::string title) {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        if (w.id == id) {
            w.title = std::move(title);
            storage_->save_workspace(*ws_current_);
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
            storage_->save_workspace(*ws_current_);
            emit(WorkspaceEvent::Type::TabAdded, ws_current_->id, window, id);
            return id;
        }
    }
    throw std::runtime_error("window not found");
}

bool WorkspaceCore::remove_tab(const WindowId& window, const TabId& tab) {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        if (w.id == window) {
            w.tabs.erase(std::remove_if(w.tabs.begin(), w.tabs.end(),
                                        [&](const Tab& t) { return t.id == tab; }),
                         w.tabs.end());
            if (w.focus_tab_id == tab) w.focus_tab_id.reset();
            storage_->save_workspace(*ws_current_);
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
            storage_->save_workspace(*ws_current_);
            emit(WorkspaceEvent::Type::PaneSplit, ws_current_->id, w.id, tab, new_id);
            return new_id;
        }
    }
    throw std::runtime_error("tab not found");
}

bool WorkspaceCore::remove_pane(const TabId& tab, const PaneId& pane) {
    (void)tab;
    (void)pane;
    // Removal of an arbitrary leaf while preserving the tree is non-trivial;
    // deferred to a later phase. No-op for now.
    return false;
}

bool WorkspaceCore::focus_pane(const TabId& tab, const PaneId& pane) {
    if (!ws_current_) return false;
    for (auto& w : ws_current_->windows) {
        if (w.focus_tab_id != tab) continue;
        for (auto& t : w.tabs) {
            if (t.id == tab && find_pane(&t.pane_tree, pane)) {
                w.focus_pane_id = pane;
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
            auto* hit = find_pane(&t.pane_tree, pane);
            // Climb up: the ratio lives on the *split* node, not the leaf.
            // For simplicity we search splits by locating parent — deferred.
            // Simplification: adjust via a recursive pass is omitted here.
            (void)hit;
            return false;
        }
    }
    return false;
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
    storage_->save_workspace(*ws_current_);
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
    if (ws_current_) {
        emit(WorkspaceEvent::Type::StateDirty, ws_current_->id);
    }
}

} // namespace remin::core
