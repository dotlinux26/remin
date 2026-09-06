#include "core/id.hpp"
#include "core/serialization.hpp"

#include <iostream>
#include <cstdlib>

static int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL: " << #cond << " at " << __LINE__ << "\n";  \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

using namespace remin::core;

int main() {
    // Id uniqueness + round-trip.
    auto w1 = WorkspaceId::generate();
    auto w2 = WorkspaceId::generate();
    CHECK(w1 != w2);
    CHECK(WorkspaceId{w1.str()} == w1);

    // ISO8601 round-trip.
    auto now = std::chrono::system_clock::now();
    auto s = to_iso8601(now);
    auto back = from_iso8601(s);
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - back).count();
    CHECK(std::abs(delta) <= 1);

    // TabKind + interrupted-source round-trip.
    CHECK(tab_kind_from_string(to_string(TabKind::Terminal)) == TabKind::Terminal);
    CHECK(tab_kind_from_string(to_string(TabKind::Note)) == TabKind::Note);
    CHECK(interrupted_source_from_string(to_string(InterruptedCommand::Source::CtrlC)) ==
          InterruptedCommand::Source::CtrlC);
    CHECK(interrupted_source_from_string(to_string(InterruptedCommand::Source::Unknown)) ==
          InterruptedCommand::Source::Unknown);

    // -- Full workspace round-trip (P1 core) -------------------------------
    Workspace ws = Workspace::create("GitLab Audit");
    ws.generation = 42;
    ws.schema_version = Workspace::kSchemaVersion;
    ws.ui.directory_tree.current_dir = "/home/user/research";
    ws.ui.directory_tree.expanded = {"/Volumes", "/Volumes/Backup/project"};
    ws.ui.directory_tree.selected = "/home/user/research/exploit/poc.py";
    ws.ui.directory_tree.filter = "expl",
    ws.ui.directory_tree.scroll_anchor = "/home/user/research/exploit";
    ws.ui.directory_tree.anchor_offset = 3.0;
    ws.focus_window_id = WindowId{"win-root"};

    Window win = Window::create("Window 1", WindowId{"win-1"});
    win.width = 1280;
    win.height = 800;
    win.x = 10;
    win.y = 20;

    // Terminal tab with a real populated pane state.
    Tab tterm = Tab::create("Recon");
    CHECK(tterm.kind == TabKind::Terminal);
    Pane p;
    p.id = PaneId{"pane-1"};
    p.state.cwd = "/home/user/research/gitlab";
    p.state.shell = "/bin/bash";
    p.state.cols = 120;
    p.state.rows = 36;
    p.state.command_history = {
        {"pwd", 1000}, {"nmap -sV 10.10.10.5", 2000}, {"ffuf -u http://10.10.10.5/FUZZ", 3000}};
    p.state.scrollback = "user@host:~$ pwd\n/home/user/research/gitlab\n";
    p.state.interrupted_command =
        InterruptedCommand{"ffuf -u http://10.10.10.5/FUZZ", 1234567,
                           InterruptedCommand::Source::CtrlC};
    tterm.pane_tree = PaneTree::leaf(std::move(p));

    // Note tab with full note state.
    Tab tnote = Tab::create("Findings", TabId{"tab-note-1"});
    tnote.kind = TabKind::Note;
    NoteTabState ns;
    ns.document_id = "doc-1";
    ns.path = "/home/user/research/notes/findings.md";
    ns.title = "Findings";
    ns.content = "# Findings\n\n- Host: 10.10.10.5\n";
    ns.modified = true;
    ns.cursor = 23;
    ns.scroll = 0.42;
    ns.preview_enabled = true;
    ns.split_ratio = 0.6;
    ns.sync_scroll = true;
    tnote.note_state = ns;

    win.tabs = {tterm, tnote};
    win.focus_tab_id = TabId{"tab-note-1"};
    win.focus_pane_id = PaneId{"pane-1"};
    ws.windows = {win};
    ws.focus_window_id = WindowId{"win-1"};

    json j;
    to_json(j, ws);
    Workspace loaded;
    from_json(j, loaded);

    CHECK(loaded.name == "GitLab Audit");
    CHECK(loaded.generation == 42);
    CHECK(loaded.schema_version == Workspace::kSchemaVersion);
    CHECK(loaded.windows.size() == 1);
    CHECK(loaded.ui.directory_tree.current_dir.string() == "/home/user/research");
    CHECK(loaded.ui.directory_tree.filter == "expl");
    CHECK(loaded.ui.directory_tree.anchor_offset == 3.0);
    CHECK(loaded.ui.directory_tree.expanded.size() == 2);
    CHECK(loaded.focus_window_id && loaded.focus_window_id->str() == "win-1");

    const auto& w = loaded.windows.front();
    CHECK(w.width == 1280 && w.height == 800 && w.x == 10 && w.y == 20);
    CHECK(w.tabs.size() == 2);
    CHECK(w.focus_tab_id && w.focus_tab_id->str() == "tab-note-1");
    CHECK(w.focus_pane_id && w.focus_pane_id->str() == "pane-1");

    const auto& term = w.tabs.front();
    CHECK(term.kind == TabKind::Terminal);
    CHECK(!term.note_state.has_value());
    CHECK(term.pane_tree.kind() == PaneTree::Kind::Pane);
    const auto& ps = term.pane_tree.pane()->state;
    CHECK(ps.cwd == "/home/user/research/gitlab");
    CHECK(ps.shell == "/bin/bash");
    CHECK(ps.cols == 120 && ps.rows == 36);
    CHECK(ps.command_history.size() == 3);
    CHECK(ps.command_history[0].command == "pwd");
    CHECK(ps.command_history[0].timestamp_us == 1000);
    CHECK(ps.command_history[2].command == "ffuf -u http://10.10.10.5/FUZZ");
    CHECK(ps.command_history[2].timestamp_us == 3000);
    CHECK(ps.scrollback.rfind("user@host:~$ pwd", 0) == 0);
    CHECK(ps.interrupted_command.has_value());
    CHECK(ps.interrupted_command->source == InterruptedCommand::Source::CtrlC);
    CHECK(ps.interrupted_command->timestamp_us == 1234567);
    // Environment must NOT be serialized (design §3.2).
    CHECK(!j.at("windows")[0].at("tabs")[0].at("pane_tree").at("pane").at("state").contains("environment"));

    const auto& note = w.tabs[1];
    CHECK(note.kind == TabKind::Note);
    CHECK(note.note_state.has_value());
    CHECK(note.note_state->document_id == "doc-1");
    CHECK(note.note_state->path == "/home/user/research/notes/findings.md");
    CHECK(note.note_state->modified == true);
    CHECK(note.note_state->cursor == 23);
    CHECK(note.note_state->scroll == 0.42);
    CHECK(note.note_state->preview_enabled && note.note_state->sync_scroll);
    CHECK(note.note_state->split_ratio == 0.6);

    // -- Migration from legacy (pre-v0.0.4) JSON ---------------------------
    // Old format: no schema_version/generation/ui, no tab kind/note_state,
    // and interrupted_command was a bare string.
    json legacy = {
        {"id", "legacy-ws"},
        {"name", "Old"},
        {"working_directory", "/tmp"},
        {"created_at", "2026-01-01T00:00:00.000Z"},
        {"last_saved_at", "2026-01-01T00:00:00.000Z"},
        {"tags", json::array()},
        {"windows", json::array({
            {
                {"id", "w"}, {"title", "W"}, {"x", 0}, {"y", 0},
                {"width", 0}, {"height", 0},
                {"tabs", json::array({
                    {
                        {"id", "t1"}, {"title", "T1"},
                        {"pane_tree", {
                            {"kind", "pane"},
                            {"pane", {
                                {"id", "p1"},
                                {"state", {
                                    {"cwd", "/tmp"}, {"shell", ""},
                                    {"cols", 80}, {"rows", 24},
                                    {"command_history", json::array({"ls"})},
                                    {"scrollback", "x"},
                                    {"interrupted_command", "vim"}
                                }}
                            }}
                        }}
                    }
                })}
            }
        })}
    };
    json upgraded = legacy;
    migrate_workspace_json(upgraded);
    CHECK(upgraded.value("schema_version", 0) == Workspace::kSchemaVersion);
    CHECK(upgraded.value("generation", 99u) == 0u);
    CHECK(upgraded.contains("ui"));
    const auto& legacy_tab = upgraded.at("windows")[0].at("tabs")[0];
    CHECK(legacy_tab.value("kind", "") == "terminal");
    CHECK(legacy_tab.at("note_state").is_null());
    CHECK(legacy_tab.at("pane_tree").at("pane").at("state").at("interrupted_command").at("source") == "unknown");

    Workspace legacy_ws;
    from_json(upgraded, legacy_ws);
    CHECK(legacy_ws.id == WorkspaceId{"legacy-ws"});
    CHECK(legacy_ws.schema_version == Workspace::kSchemaVersion);
    CHECK(legacy_ws.generation == 0);
    CHECK(legacy_ws.windows.size() == 1);
    const auto& lt = legacy_ws.windows[0].tabs[0];
    CHECK(lt.kind == TabKind::Terminal);
    CHECK(!lt.note_state.has_value());
    CHECK(lt.pane_tree.pane()->state.interrupted_command.has_value());
    CHECK(lt.pane_tree.pane()->state.interrupted_command->source == InterruptedCommand::Source::Unknown);
    CHECK(lt.pane_tree.pane()->state.interrupted_command->command == "vim");
    // Legacy `command_history` is a plain string array → migrated to records
    // with an unknowable (0) timestamp.
    CHECK(lt.pane_tree.pane()->state.command_history.size() == 1);
    CHECK(lt.pane_tree.pane()->state.command_history[0].command == "ls");
    CHECK(lt.pane_tree.pane()->state.command_history[0].timestamp_us == 0);

    if (g_failures == 0) {
        std::cout << "serialization_test: OK\n";
        return 0;
    }
    std::cerr << "serialization_test: " << g_failures << " failure(s)\n";
    return 1;
}
