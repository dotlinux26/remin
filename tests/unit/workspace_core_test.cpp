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
    auto initial_pane = PaneTree::leaf(Pane{PaneId::generate(), PaneState{}});
    auto tab_id = core.add_tab(win_id, "Recon", std::move(initial_pane));
    CHECK(!tab_id.empty());

    // Split it.
    auto pane2 = core.split_pane(tab_id, PaneTree::Kind::SplitVertical, 0.5);
    CHECK(!pane2.empty());

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

    // Workspace was persisted to the fake storage.
    CHECK(storage.workspaces.size() >= 1);

    if (g_failures == 0) {
        std::cout << "workspace_core_test: OK\n";
        return 0;
    }
    std::cerr << "workspace_core_test: " << g_failures << " failure(s)\n";
    return 1;
}
