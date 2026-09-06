#include "core/workspace_core.hpp"
#include "core/pane/pane.hpp"
#include "core/serialization.hpp"

#include <iostream>
#include <string>
#include <vector>

// Minimal test harness (mirrors workspace_core_test.cpp).
static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL: " << #cond << " at " << __LINE__ << "\n";      \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

using namespace remin::core;

// Minimal in-memory Storage fake for testing WorkspaceCore without SQLite.
class FakeStorage : public Storage {
public:
    std::vector<Workspace> workspaces;
    std::vector<Snapshot> snapshots;

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
    void save_snapshot(const WorkspaceId&, const Snapshot&, const json&) override {}
    void delete_snapshot(const WorkspaceId&, const SnapshotId&) override {}
    void store_scrollback(const PaneId&, std::string) override {}
    std::string load_scrollback(const PaneId&) override { return {}; }

    // Closed-window history (stubs)
    void store_closed_window(const ClosedWindowSnapshot&) override {}
    std::vector<ClosedWindowSnapshot> list_closed_windows(const WorkspaceId&) override { return {}; }
    std::optional<ClosedWindowSnapshot> load_closed_window(const WorkspaceId&, const SnapshotId&) override { return std::nullopt; }
    void delete_closed_window(const WorkspaceId&, const SnapshotId&) override {}

    bool checkpoint(const WorkspaceId&, const json&, int, int64_t, const std::string&,
                    const std::vector<std::pair<PaneId, std::string>>&) override {
        return true;
    }
};

int main() {
    FakeStorage storage;
    WorkspaceCore core(&storage);

    auto ws_id = core.create_workspace("history-lab");
    auto win_id = core.add_window("Audit");

    // Tab with two panes (split): per-pane history must stay separate.
    auto pane1 = Pane{PaneId::generate(), PaneState{}};
    auto tab_id = core.add_tab(win_id, "Recon", PaneTree::leaf(pane1));
    auto pane2 = core.split_pane(tab_id, PaneTree::Kind::SplitVertical, 0.5);

    // -- add_command_to_pane appends to the correct pane only --
    CHECK(core.add_command_to_pane(tab_id, pane1.id, CommandRecord{"pwd", 1111}));
    CHECK(core.add_command_to_pane(tab_id, pane1.id, CommandRecord{"nmap -sV 10.0.0.1", 2222}));
    CHECK(core.add_command_to_pane(tab_id, pane2, CommandRecord{"ffuf -u http://10.0.0.1/FUZZ", 3333}));

    auto aggregate = aggregate_command_history(*core.current_workspace());
    CHECK(aggregate.size() == 3);
    if (aggregate.size() == 3) {
        CHECK(aggregate[0].tab == tab_id);
        CHECK(aggregate[0].pane == pane1.id);
        CHECK(aggregate[0].record.command == "pwd");
        CHECK(aggregate[0].record.timestamp_us == 1111);
        CHECK(aggregate[1].record.command == "nmap -sV 10.0.0.1");
        CHECK(aggregate[2].pane == pane2);
        CHECK(aggregate[2].record.command == "ffuf -u http://10.0.0.1/FUZZ");
        CHECK(aggregate[2].record.timestamp_us == 3333);
    }

    // -- Adjacent dedupe: re-running Up+Enter (the last command) must not duplicate,
    //    even with a fresh timestamp --
    CHECK(core.add_command_to_pane(tab_id, pane1.id, CommandRecord{"nmap -sV 10.0.0.1", 4444}));
    CHECK(aggregate_command_history(*core.current_workspace()).size() == 3);
    // A repeat that is NOT adjacent is kept.
    CHECK(core.add_command_to_pane(tab_id, pane1.id, CommandRecord{"whoami", 5555}));
    CHECK(core.add_command_to_pane(tab_id, pane1.id, CommandRecord{"pwd", 6666}));
    CHECK(aggregate_command_history(*core.current_workspace()).size() == 5);

    // -- Empty commands and unknown ids are rejected --
    CHECK(!core.add_command_to_pane(tab_id, pane1.id, CommandRecord{"", 0}));
    CHECK(!core.add_command_to_pane(TabId{"nope"}, pane1.id, CommandRecord{"ls", 0}));
    CHECK(!core.add_command_to_pane(tab_id, PaneId{"nope"}, CommandRecord{"ls", 0}));

    // -- Cap: only the newest kMaxCommandHistoryPerPane entries survive --
    for (std::size_t i = 1; i <= 1200; ++i) {
        core.add_command_to_pane(tab_id, pane2, CommandRecord{"cmd-" + std::to_string(i),
                                                              static_cast<std::int64_t>(i)});
    }
    {
        const Workspace* ws = core.current_workspace();
        const PaneTree* tree = nullptr;
        for (const auto& w : ws->windows)
            for (const auto& t : w.tabs) if (t.id == tab_id) tree = &t.pane_tree;
        std::vector<const Pane*> v;
        if (tree) tree->collect_panes(v);
        CHECK(v.size() == 2);
        if (v.size() == 2) {
            const Pane* p2 = v[0]->id == pane2 ? v[0] : v[1];
            const Pane* p1 = v[0]->id == pane1.id ? v[0] : v[1];
            // 1200 pushes, cap 1000 → keeps the newest 1000 ("cmd-201".."cmd-1200").
            CHECK(p2->state.command_history.size() == 1000);
            if (!p2->state.command_history.empty()) {
                CHECK(p2->state.command_history.front().command == "cmd-201");
                // Timestamps survive the cap alongside their command.
                CHECK(p2->state.command_history.front().timestamp_us == 201);
                CHECK(p2->state.command_history.back().command == "cmd-1200");
                CHECK(p2->state.command_history.back().timestamp_us == 1200);
            }
            // pane1's own history is untouched by the cap on pane2.
            CHECK(p1->state.command_history.size() == 4);
        }
    }
    // Aggregate covers both panes: pane1(4) + pane2(1000).
    CHECK(aggregate_command_history(*core.current_workspace()).size() == 1004);

    // -- clear_command_history persists a clear over every pane --
    CHECK(core.clear_command_history());
    CHECK(aggregate_command_history(*core.current_workspace()).empty());
    CHECK(core.dirty());

    if (g_failures == 0) {
        std::cout << "history_test: OK\n";
        return 0;
    }
    std::cerr << "history_test: " << g_failures << " failure(s)\n";
    return 1;
}