#include "core/workspace_core.hpp"
#include "core/pane/pane.hpp"
#include "core/serialization.hpp"

#include <iostream>
#include <string>
#include <algorithm>

// Minimal test harness (no GoogleTest until suite grows).
static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL: " << #cond << " at " << __LINE__ << "\n";      \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

using namespace remin::core;

// A minimal in-memory Storage fake for testing WorkspaceCore logic without SQLite.
class FakeStorage : public Storage {
public:
    struct Snap { WorkspaceId id; json state; };

    std::vector<Workspace> workspaces;
    std::vector<Snap> snapshots;   // (workspace_id, json state)

    std::vector<Workspace> list_workspaces() override { return workspaces; }
    std::optional<Workspace> load_workspace(const WorkspaceId& id) override {
        for (const auto& w : workspaces) if (w.id == id) return w;
        return std::nullopt;
    }
    void save_workspace(const Workspace& ws) override {
        bool found = false;
        for (auto& w : workspaces) if (w.id == ws.id) { w = ws; found = true; break; }
        if (!found) workspaces.push_back(ws);
    }
    void delete_workspace(const WorkspaceId& id) override {
        workspaces.erase(
            std::remove_if(workspaces.begin(), workspaces.end(),
                           [&](const Workspace& w) { return w.id == id; }),
            workspaces.end());
    }

    std::vector<Snapshot> list_snapshots(const WorkspaceId&) override { return {}; }
    std::optional<json> load_snapshot(const WorkspaceId&, const SnapshotId&) override { return std::nullopt; }
    void save_snapshot(const WorkspaceId& id, const Snapshot&, const json& state) override {
        snapshots.push_back({id, state});
    }    void delete_snapshot(const WorkspaceId&, const SnapshotId&) override {}

    void store_scrollback(const PaneId&, std::string) override {}
    std::string load_scrollback(const PaneId&) override { return {}; }

    bool checkpoint(const WorkspaceId&, const json&, int, int64_t, const std::string&,
                    const std::vector<std::pair<PaneId, std::string>>&) override {
        return true;
    }
};

int main() {
    FakeStorage storage;
    WorkspaceCore core(&storage);

    // Create + open workspace.
    auto ws_id = core.create_workspace("pentest-lab");
    CHECK(!ws_id.empty());
    CHECK(core.current_workspace() != nullptr);
    CHECK(core.current_workspace()->name == "pentest-lab");

    // Add a window.
    auto win_id = core.add_window("GitLab Audit");
    CHECK(!win_id.empty());

    // Add a tab with a single pane.
    auto pane1 = Pane{PaneId::generate(), PaneState{}};
    auto initial_pane = PaneTree::leaf(pane1);
    auto tab_id = core.add_tab(win_id, "Recon", std::move(initial_pane));
    CHECK(!tab_id.empty());

    // Split it.
    auto pane2 = core.split_pane(tab_id, PaneTree::Kind::SplitVertical, 0.5);
    CHECK(!pane2.empty());

    // Count panes in the tab tree.
    auto tree_of = [&](const TabId& tid) -> PaneTree& {
        for (auto& w : core.current_workspace()->windows)
            for (auto& t : w.tabs) if (t.id == tid) return t.pane_tree;
        throw std::runtime_error("tab missing");
    };
    auto count_panes = [](const PaneTree& t) {
        std::vector<const Pane*> v;
        t.collect_panes(v);
        return v.size();
    };
    CHECK(count_panes(tree_of(tab_id)) == 2);

    // set_pane_ratio finds the parent split and stores the ratio.
    CHECK(core.set_pane_ratio(tab_id, pane2, 0.25));
    const auto& tree = tree_of(tab_id);
    const PaneTree* split = nullptr;
    // The root must be a split now (pane1 | pane2).
    if (tree.kind() != PaneTree::Kind::Pane) split = &tree;
    CHECK(split != nullptr);
    if (split) CHECK(split->ratio() == 0.25);

    // Removing a pane collapses the split back to a single pane.
    CHECK(core.remove_pane(tab_id, pane2));
    CHECK(count_panes(tree_of(tab_id)) == 1);
    CHECK(tree_of(tab_id).kind() == PaneTree::Kind::Pane);
    CHECK(tree_of(tab_id).pane() && tree_of(tab_id).pane()->id == pane1.id);

    // Removing the last pane in a split-rooted tree with multiple panes works.
    auto pane3 = core.split_pane(tab_id, PaneTree::Kind::SplitHorizontal, 0.5);
    CHECK(!pane3.empty());
    CHECK(count_panes(tree_of(tab_id)) == 2);
    CHECK(core.remove_pane(tab_id, pane3));
    CHECK(count_panes(tree_of(tab_id)) == 1);

    // Verify the current workspace serializes and round-trips.
    json j;
    to_json(j, *core.current_workspace());
    Workspace restored;
    from_json(j, restored);
    CHECK(restored.id == core.current_workspace()->id);
    CHECK(restored.name == "pentest-lab");
    CHECK(restored.windows.size() == 1);

    // Snapshot creates a persisted state.
    auto snap_id = core.create_snapshot();
    CHECK(!snap_id.empty());
    CHECK(storage.snapshots.size() == 1);

    // D-2: workspace mutations mark dirty but do NOT write until persist().
    // The initial create_workspace wrote one row.
    CHECK(storage.workspaces.size() == 1);
    // A structural mutation dirties state without an inline save.
    auto win2 = core.add_window("second window");
    CHECK(!win2.empty());
    CHECK(core.dirty() == true);
    // No eager persist: fake storage still only has the single row, and it does
    // NOT yet contain the second window.
    CHECK(storage.workspaces.size() == 1);
    {
        auto saved = storage.workspaces.front();
        bool has_win2 = false;
        for (const auto& w : saved.windows) if (w.id == win2) { has_win2 = true; break; }
        CHECK(has_win2 == false);
    }
    // persist() writes the current live workspace and clears the dirty flag.
    CHECK(core.persist() == true);
    CHECK(core.dirty() == false);
    CHECK(storage.workspaces.size() == 1);  // upsert, not append
    {
        auto saved = storage.workspaces.front();
        bool has_win2 = false;
        for (const auto& w : saved.windows) if (w.id == win2) { has_win2 = true; break; }
        CHECK(has_win2 == true);
    }
    // persist() with a clean state is a no-op that still reports success.
    auto before = storage.workspaces.size();
    CHECK(core.persist() == true);
    CHECK(storage.workspaces.size() == before);

    // Workspace was persisted to the fake storage.
    CHECK(storage.workspaces.size() >= 1);

    if (g_failures == 0) {
        std::cout << "workspace_core_test: OK\n";
        return 0;
    }
    std::cerr << "workspace_core_test: " << g_failures << " failure(s)\n";
    return 1;
}
