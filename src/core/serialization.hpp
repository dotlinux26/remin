#pragma once

#include "core/workspace/workspace.hpp"
#include "core/snapshot/snapshot.hpp"
#include "core/pane/pane.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <string>

namespace remin::core {

using json = nlohmann::json;

// Human-readable JSON serialization of the Remin workspace model.
// This is the interchange/export/debug format. Canonical storage is SQLite;
// JSON is used for .remin export, IPC payloads, and debugging.

[[nodiscard]] std::string to_iso8601(std::chrono::system_clock::time_point tp);
[[nodiscard]] std::chrono::system_clock::time_point from_iso8601(const std::string& s);

// -- TabKind --------------------------------------------------------------

inline const char* to_string(TabKind k) {
    switch (k) {
        case TabKind::Note: return "note";
        case TabKind::Terminal: return "terminal";
    }
    return "terminal";
}

inline TabKind tab_kind_from_string(const std::string& s) {
    return s == "note" ? TabKind::Note : TabKind::Terminal;
}

// -- InterruptedCommand ---------------------------------------------------

inline const char* to_string(InterruptedCommand::Source s) {
    switch (s) {
        case InterruptedCommand::Source::CtrlC: return "ctrl_c";
        case InterruptedCommand::Source::ProcessExit: return "process_exit";
        case InterruptedCommand::Source::Unknown: return "unknown";
    }
    return "unknown";
}

inline InterruptedCommand::Source interrupted_source_from_string(const std::string& s) {
    if (s == "ctrl_c") return InterruptedCommand::Source::CtrlC;
    if (s == "process_exit") return InterruptedCommand::Source::ProcessExit;
    return InterruptedCommand::Source::Unknown;
}

inline void to_json(json& j, const InterruptedCommand& c) {
    j = json{{"command", c.command},
             {"timestamp_us", c.timestamp_us},
             {"source", to_string(c.source)}};
}

inline void from_json(const json& j, InterruptedCommand& c) {
    c.command = j.value("command", std::string{});
    c.timestamp_us = j.value("timestamp_us", std::int64_t{0});
    c.source = interrupted_source_from_string(j.value("source", "unknown"));
}

// -- CommandRecord --------------------------------------------------------
// Older checkpoints stored `command_history` as a plain array of strings.
// `from_json` accepts both the object form we write and the legacy string
// form (timestamp is unknowable for migrated rows → 0), so reads never break
// on pre-CommandRecord data.

inline void to_json(json& j, const CommandRecord& r) {
    j = json{{"command", r.command}, {"timestamp_us", r.timestamp_us}};
}

inline void from_json(const json& j, CommandRecord& r) {
    if (j.is_string()) {
        r = CommandRecord{j.get<std::string>(), 0};
        return;
    }
    r.command = j.value("command", std::string{});
    r.timestamp_us = j.value("timestamp_us", std::int64_t{0});
}

inline void to_json(json& j, const PaneState& s) {
    j = json{
        {"cwd", s.cwd},
        {"shell", s.shell},
        {"cols", s.cols},
        {"rows", s.rows},
        {"command_history", s.command_history},
        {"scrollback", s.scrollback},
    };
    // V1 design decision: environment is intentionally NOT persisted.
    if (s.interrupted_command) j["interrupted_command"] = *s.interrupted_command;
}

inline void from_json(const json& j, PaneState& s) {
    s.cwd = j.value("cwd", std::string{});
    s.shell = j.value("shell", std::string{});
    s.cols = j.value("cols", 0u);
    s.rows = j.value("rows", 0u);
    if (j.contains("command_history")) j.at("command_history").get_to(s.command_history);
    s.scrollback = j.value("scrollback", std::string{});
    if (j.contains("interrupted_command")) {
        const auto& ic = j.at("interrupted_command");
        if (ic.is_string()) {
            // Migration: pre-v0.0.4 stores a bare command string; we do not know
            // why it stopped, so source = Unknown.
            s.interrupted_command = InterruptedCommand{ic.get<std::string>(), 0,
                                                       InterruptedCommand::Source::Unknown};
        } else {
            s.interrupted_command.emplace();
            ic.get_to(*s.interrupted_command);
        }
    }
}

inline void to_json(json& j, const Pane& p) {
    j = json{{"id", p.id.str()}, {"state", p.state}};
}

inline void from_json(const json& j, Pane& p) {
    p.id = PaneId{j.value("id", std::string{})};
    if (j.contains("state")) j.at("state").get_to(p.state);
}

[[nodiscard]] json pane_tree_to_json(const PaneTree& tree);
[[nodiscard]] PaneTree pane_tree_from_json(const json& j);

inline void to_json(json& j, const NoteTabState& s) {
    j = json{
        {"document_id", s.document_id},
        {"path", s.path},
        {"title", s.title},
        {"content", s.content},
        {"modified", s.modified},
        {"cursor", s.cursor},
        {"scroll", s.scroll},
        {"preview_enabled", s.preview_enabled},
        {"split_ratio", s.split_ratio},
        {"sync_scroll", s.sync_scroll},
    };
}

inline void from_json(const json& j, NoteTabState& s) {
    s.document_id = j.value("document_id", std::string{});
    s.path = j.value("path", std::string{});
    s.title = j.value("title", std::string{});
    s.content = j.value("content", std::string{});
    s.modified = j.value("modified", false);
    s.cursor = j.value("cursor", 0);
    s.scroll = j.value("scroll", 0.0);
    s.preview_enabled = j.value("preview_enabled", false);
    s.split_ratio = j.value("split_ratio", 0.5);
    s.sync_scroll = j.value("sync_scroll", false);
}

inline void to_json(json& j, const Tab& t) {
    j = json{
        {"id", t.id.str()},
        {"title", t.title},
        {"kind", to_string(t.kind)},
        {"pane_tree", pane_tree_to_json(t.pane_tree)},
    };
    if (t.note_state) j["note_state"] = *t.note_state;
}

inline void from_json(const json& j, Tab& t) {
    t.id = TabId{j.value("id", std::string{})};
    t.title = j.value("title", std::string{});
    // Migration: schema without "kind" (pre-v0.0.4) is a terminal tab.
    t.kind = tab_kind_from_string(j.value("kind", "terminal"));
    if (j.contains("pane_tree")) t.pane_tree = pane_tree_from_json(j.at("pane_tree"));
    if (j.contains("note_state") && !j.at("note_state").is_null())
        t.note_state = j.at("note_state").get<NoteTabState>();
}

inline void to_json(json& j, const Window& w) {
    j = json{
        {"id", w.id.str()},
        {"label", w.label},
        {"x", w.x},
        {"y", w.y},
        {"width", w.width},
        {"height", w.height},
        {"tabs", w.tabs},
    };
    if (w.focus_tab_id) j["focus_tab_id"] = w.focus_tab_id->str();
    if (w.focus_pane_id) j["focus_pane_id"] = w.focus_pane_id->str();
}

inline void from_json(const json& j, Window& w) {
    w.id = WindowId{j.value("id", std::string{})};
    // "label" is the current key; old checkpoints stored it as "title".
    if (j.contains("label")) w.label = j.at("label").get<std::string>();
    else if (j.contains("title")) w.label = j.at("title").get<std::string>();
    w.x = j.value("x", 0);
    w.x = j.value("x", 0);
    w.y = j.value("y", 0);
    w.width = j.value("width", 0u);
    w.height = j.value("height", 0u);
    if (j.contains("tabs")) j.at("tabs").get_to(w.tabs);
    if (j.contains("focus_tab_id")) w.focus_tab_id = TabId{j.value("focus_tab_id", std::string{})};
    if (j.contains("focus_pane_id")) w.focus_pane_id = PaneId{j.value("focus_pane_id", std::string{})};
}

// -- UiState ---------------------------------------------------------------

inline void to_json(json& j, const DirectoryTreeState& s) {
    std::vector<std::string> expanded;
    for (const auto& p : s.expanded) expanded.push_back(p.string());
    j = json{
        {"current_dir", s.current_dir.string()},
        {"expanded", expanded},
        {"selected", s.selected.string()},
        {"filter", s.filter},
        {"scroll_anchor", s.scroll_anchor.string()},
        {"anchor_offset", s.anchor_offset},
    };
}

inline void from_json(const json& j, DirectoryTreeState& s) {
    s.current_dir = j.value("current_dir", std::string{});
    s.selected = j.value("selected", std::string{});
    s.filter = j.value("filter", std::string{});
    s.scroll_anchor = j.value("scroll_anchor", std::string{});
    s.anchor_offset = j.value("anchor_offset", 0.0);
    if (j.contains("expanded")) {
        s.expanded.clear();
        for (const auto& e : j.at("expanded")) s.expanded.emplace_back(e.get<std::string>());
    }
}

inline void to_json(json& j, const UiState& u) {
    j = json{{"directory_tree", u.directory_tree}};
}

inline void from_json(const json& j, UiState& u) {
    if (j.contains("directory_tree")) j.at("directory_tree").get_to(u.directory_tree);
}

inline void to_json(json& j, const Workspace& w) {
    std::vector<std::string> snaps;
    for (const auto& s : w.snapshot_ids) snaps.push_back(s.str());
    j = json{
        {"id", w.id.str()},
        {"name", w.name},
        {"working_directory", w.working_directory},
        {"created_at", to_iso8601(w.created_at)},
        {"last_saved_at", to_iso8601(w.last_saved_at)},
        {"schema_version", w.schema_version},
        {"generation", w.generation},
        {"tags", w.tags},
        {"windows", w.windows},
        {"ui", w.ui},
        {"snapshots", snaps},
    };
    if (w.focus_window_id) j["focus_window_id"] = w.focus_window_id->str();
}

inline void from_json(const json& j, Workspace& w) {
    w.id = WorkspaceId{j.value("id", std::string{})};
    w.name = j.value("name", std::string{});
    w.working_directory = j.value("working_directory", std::string{});
    w.created_at = from_iso8601(j.value("created_at", std::string{}));
    w.last_saved_at = from_iso8601(j.value("last_saved_at", std::string{}));
    w.schema_version = j.value("schema_version", Workspace::kSchemaVersion);
    w.generation = j.value("generation", 0u);
    w.tags = j.value("tags", std::vector<std::string>{});
    if (j.contains("windows")) j.at("windows").get_to(w.windows);
    if (j.contains("ui")) w.ui = j.at("ui").get<UiState>();
    if (j.contains("snapshots")) {
        w.snapshot_ids.clear();
        for (const auto& s : j.at("snapshots")) w.snapshot_ids.push_back(SnapshotId{s.get<std::string>()});
    }
    if (j.contains("focus_window_id")) w.focus_window_id = WindowId{j.value("focus_window_id", std::string{})};
}

// -- Migration -------------------------------------------------------------
//
// Upgrades legacy workspace JSON in place to the current schema:
//   - missing schema_version/generation/ui      → defaults
//   - tabs without "kind" / "note_state"        → terminal, no note state
// (interrupted_command string→object is handled inside PaneState::from_json.)
// Storage/snapshot readers call this before from_json where they need the JSON
// rewritten (so a re-save is forward compatible), while from_json alone is
// already tolerant of missing fields for pure reads.
inline void migrate_workspace_json(json& j) {
    j["schema_version"] = j.value("schema_version", Workspace::kSchemaVersion);
    j["generation"] = j.value("generation", 0u);
    if (!j.contains("ui")) j["ui"] = json::object();
    const auto migrate_window = [](json& win) {
        if (!win.contains("tabs")) return;
        for (auto& tab : win.at("tabs")) {
            if (!tab.contains("kind")) tab["kind"] = "terminal";
            if (!tab.contains("note_state")) tab["note_state"] = nullptr;
            if (tab.contains("pane_tree") && tab.at("pane_tree").contains("pane") &&
                tab.at("pane_tree").at("pane").contains("state")) {
                auto& st = tab.at("pane_tree").at("pane").at("state");
                if (st.contains("interrupted_command") && st.at("interrupted_command").is_string()) {
                    st["interrupted_command"] =
                        json{{"command", st.at("interrupted_command").get<std::string>()},
                             {"timestamp_us", 0}, {"source", "unknown"}};
                }
            }
        }
    };
    if (j.contains("windows")) {
        for (auto& win : j.at("windows")) migrate_window(win);
    }
}

inline void to_json(json& j, const ClosedWindowSnapshot& s) {
    j = json{
        {"id", s.id.str()},
        {"workspace_id", ""},  // filled by storage layer
        {"window_id", s.window_id.str()},
        {"label", s.label},
        {"closed_at", to_iso8601(s.closed_at)},
        {"state_json", s.workspace_state_json},
        {"generation", s.generation},
    };
}

inline void from_json(const json& j, ClosedWindowSnapshot& s) {
    s.id = SnapshotId{j.value("id", std::string{})};
    s.window_id = WindowId{j.value("window_id", std::string{})};
    s.label = j.value("label", std::string{});
    s.closed_at = from_iso8601(j.value("closed_at", std::string{}));
    s.workspace_state_json = j.value("state_json", std::string{});
    s.generation = j.value("generation", std::uint64_t{0});
}

} // namespace remin::core
