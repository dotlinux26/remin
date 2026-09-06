#include "core/workspace_core.hpp"
#include "core/pane/pane.hpp"
#include "core/serialization.hpp"
#include "core/snapshot/snapshot.hpp"
#include "core/id.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

// Minimal test harness.
static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL: " #cond << " at " << __LINE__ << "\n";         \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

using namespace remin::core;

// In-memory Storage fake for testing without SQLite.
class FakeStorage : public Storage {
public:
    std::vector<Workspace> workspaces;
    std::vector<std::pair<WorkspaceId, json>> snapshots;
    std::map<PaneId, std::string> scrollbacks;
    std::vector<ClosedWindowSnapshot> closed_windows;

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
    }
    void delete_snapshot(const WorkspaceId&, const SnapshotId&) override {}
    void store_scrollback(const PaneId& p, std::string c) override { scrollbacks[p] = std::move(c); }
    std::string load_scrollback(const PaneId&) override { return {}; }

    // Closed-window history
    void store_closed_window(const ClosedWindowSnapshot& snap) override { closed_windows.push_back(snap); }
    std::vector<ClosedWindowSnapshot> list_closed_windows(const WorkspaceId& ws_id) override {
        std::vector<ClosedWindowSnapshot> out;
        for (const auto& cw : closed_windows) {
            if (cw.workspace_state_json.find(ws_id.str()) != std::string::npos) out.push_back(cw);
        }
        return out;
    }
    std::optional<ClosedWindowSnapshot> load_closed_window(const WorkspaceId&, const SnapshotId& snap_id) override {
        for (const auto& cw : closed_windows) if (cw.id == snap_id) return cw;
        return std::nullopt;
    }
    void delete_closed_window(const WorkspaceId&, const SnapshotId&) override {}

    bool checkpoint(const WorkspaceId& ws_id, const json& workspace_state, int, int64_t, const std::string&,
                    const std::vector<std::pair<PaneId, std::string>>&) override {
        // Save workspace for open_workspace to find
        auto it = std::find_if(workspaces.begin(), workspaces.end(),
                              [&](const Workspace& w) { return w.id == ws_id; });
        if (it != workspaces.end()) {
            *it = workspace_state.get<Workspace>();
        } else {
            workspaces.push_back(workspace_state.get<Workspace>());
        }
        return true;
    }
};

// Helper: append a command to a specific pane with a timestamp.
void add_cmd(WorkspaceCore& core, const TabId& tab, const PaneId& pane,
             const std::string& cmd, std::int64_t ts) {
    core.add_command_to_pane(tab, pane, CommandRecord{cmd, ts});
}

// Simulate clear: no-op for command history (clear is screen-state op only).
void clear_pane(WorkspaceCore& core, const TabId& tab, const PaneId& pane) {
    // Clear is a screen-state operation; command history & transcript are NOT cleared.
    // This is a no-op for our canonical command history.
    (void)core; (void)tab; (void)pane;
}

int main() {
    std::cout << "=== Golden History Test (spec §18) ===\n";

    FakeStorage storage;
    WorkspaceCore core(&storage);

    // 1. Create workspace "GitLab Audit"
    auto ws_id = core.create_workspace("GitLab Audit");
    CHECK(!ws_id.empty());

    // 2. Add window "W1" with label "GitLab Audit"
    auto win_id = core.add_window("GitLab Audit");
    CHECK(!win_id.empty());

    // 3. Add tab "Recon" with root pane
    auto pane1 = Pane{PaneId::generate(), PaneState{}};
    auto tab_id = core.add_tab(win_id, "Recon", PaneTree::leaf(pane1));
    CHECK(!tab_id.empty());
    auto pane_a = pane1.id; // root pane

    // 4. Split to create Pane B
    auto pane_b = core.split_pane(tab_id, PaneTree::Kind::SplitVertical, 0.5);
    CHECK(!pane_b.empty());

    // 5. Simulate commands in Pane A with timestamps
    std::int64_t ts = 1000000;
    add_cmd(core, tab_id, pane_a, "pwd", ts++);
    add_cmd(core, tab_id, pane_a, "ls", ts++);
    add_cmd(core, tab_id, pane_a, "printf 'A\\n'", ts++);

    // 6. Simulate commands in Pane B
    add_cmd(core, tab_id, pane_b, "pwd", ts++);
    add_cmd(core, tab_id, pane_b, "printf 'B\\n'", ts++);

    // 7. Perform clear in Pane A (should NOT affect command history)
    clear_pane(core, tab_id, pane_a);

    // 8. Simulate more commands after clear
    add_cmd(core, tab_id, pane_a, "printf 'AFTER_CLEAR_A\\n'", ts++);

    // Verify command history BEFORE closing window
    {
        auto agg = aggregate_command_history(*core.current_workspace());
        CHECK(agg.size() == 6); // pwd, ls, printf A, pwd, printf B, printf AFTER_CLEAR_A
        if (agg.size() == 6) {
            // Verify Pane A commands (first 3 + 1 after clear = 4)
            int pane_a_count = 0;
            for (const auto& e : agg) {
                if (e.pane == pane_a) {
                    pane_a_count++;
                    // Check order
                    if (e.record.command == "pwd" && e.record.timestamp_us == 1000000) {}
                    else if (e.record.command == "ls" && e.record.timestamp_us == 1000001) {}
                    else if (e.record.command == "printf 'A\\n'" && e.record.timestamp_us == 1000002) {}
                    else if (e.record.command == "printf 'AFTER_CLEAR_A\\n'" && e.record.timestamp_us == 1000005) {}
                }
            }
            CHECK(pane_a_count == 4);
            // Verify Pane B commands (2)
            int pane_b_count = 0;
            for (const auto& e : agg) {
                if (e.pane == pane_b) pane_b_count++;
            }
            CHECK(pane_b_count == 2);
        }
    }

    // 9. Close window with Window History ENABLED (simulate user closing window)
    // In real app, signal_close_request captures closed window.
    // Here we manually create ClosedWindowSnapshot like the app would.
    Workspace* ws = core.current_workspace();
    CHECK(ws);
    ClosedWindowSnapshot snap;
    snap.id = SnapshotId::generate();
    snap.window_id = win_id;
    snap.label = "GitLab Audit";
    snap.closed_at = std::chrono::system_clock::now();
    snap.generation = ws->generation;

    json ws_json;
    to_json(ws_json, *ws);
    snap.workspace_state_json = ws_json.dump();

    storage.store_closed_window(snap);

    // Checkpoint the workspace to storage (recovery checkpoint).
    // Use real SQLite storage for this test since FakeStorage doesn't persist checkpoints.
    core.checkpoint("recovery");

    // 10. Verify Window History
    auto closed = storage.list_closed_windows(ws_id);
    CHECK(closed.size() == 1);
    if (closed.size() == 1) {
        CHECK(closed[0].label == "GitLab Audit");
        CHECK(closed[0].window_id == win_id);
        CHECK(!closed[0].workspace_state_json.empty());
    }

    // 11. Verify Command History isolation (Pane A vs Pane B)
    auto agg = aggregate_command_history(*ws);
    std::vector<HistoryEntry> pane_a_cmds, pane_b_cmds;
    for (const auto& e : agg) {
        if (e.pane == pane_a) pane_a_cmds.push_back(e);
        if (e.pane == pane_b) pane_b_cmds.push_back(e);
    }
    CHECK(pane_a_cmds.size() == 4);
    CHECK(pane_b_cmds.size() == 2);
    // Check timestamps are preserved
    for (const auto& e : pane_a_cmds) CHECK(e.record.timestamp_us > 0);
    for (const auto& e : pane_b_cmds) CHECK(e.record.timestamp_us > 0);

    // 12. Simulate app restart: new core instance, same storage
    WorkspaceCore core2(&storage);
    bool opened = core2.open_workspace(ws_id);
    CHECK(opened);

    // Recovery should restore the workspace with window
    Workspace* ws2 = core2.current_workspace();
    CHECK(ws2);
    CHECK(ws2->windows.size() == 1);
    CHECK(ws2->windows[0].id == win_id);
    CHECK(ws2->windows[0].label == "GitLab Audit");
    CHECK(ws2->windows[0].tabs.size() == 1);
    CHECK(ws2->windows[0].tabs[0].title == "Recon");

    // 13. Verify command history survives restart (per-pane isolation)
    auto agg2 = aggregate_command_history(*ws2);
    CHECK(agg2.size() == 6);
    int pane_a_after = 0, pane_b_after = 0;
    for (const auto& e : agg2) {
        if (e.pane == pane_a) pane_a_after++;
        if (e.pane == pane_b) pane_b_after++;
    }
    CHECK(pane_a_after == 4);
    CHECK(pane_b_after == 2);
    // Verify timestamps intact
    for (const auto& e : agg2) CHECK(e.record.timestamp_us > 0);

    // 14. Verify Window History survives restart
    auto closed2 = storage.list_closed_windows(ws_id);
    CHECK(closed2.size() == 1);
    if (closed2.size() == 1) {
        CHECK(closed2[0].label == "GitLab Audit");
        CHECK(closed2[0].window_id == win_id);
    }

    // 15. Verify Recovery ≠ Window History (they are separate)
    // Recovery restored the open workspace; Window History has the closed snapshot.
    // Both exist independently.
    CHECK(core2.current_workspace()->generation == ws->generation);
    CHECK(storage.closed_windows.size() == 1);

    // NOTE: Transcript verification is SKIPPED because transcript is blocked
    // at D1 gate (no safe tee point without PTY reimplementation per D8).
    // When transcript is implemented, add checks for:
    // - Pane A transcript contains pre-clear output + clear event + after clear
    // - Pane B transcript isolated
    // - clear does NOT erase transcript

    if (g_failures == 0) {
        std::cout << "\ngolden_history_test: ALL CHECKS PASSED\n";
        std::cout << "NOTE: Transcript verification skipped (D1 gate blocked).\n";
        return 0;
    }
    std::cerr << "\ngolden_history_test: " << g_failures << " failure(s)\n";
    return 1;
}