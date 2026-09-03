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

inline void to_json(json& j, const PaneState& s) {
    j = json{
        {"cwd", s.cwd},
        {"shell", s.shell},
        {"cols", s.cols},
        {"rows", s.rows},
        {"environment", s.environment},
        {"command_history", s.command_history},
        {"scrollback", s.scrollback},
    };
    if (s.interrupted_command) j["interrupted_command"] = *s.interrupted_command;
}

inline void from_json(const json& j, PaneState& s) {
    s.cwd = j.value("cwd", std::string{});
    s.shell = j.value("shell", std::string{});
    s.cols = j.value("cols", 0u);
    s.rows = j.value("rows", 0u);
    if (j.contains("environment")) j.at("environment").get_to(s.environment);
    if (j.contains("command_history")) j.at("command_history").get_to(s.command_history);
    s.scrollback = j.value("scrollback", std::string{});
    if (j.contains("interrupted_command")) s.interrupted_command = j.value("interrupted_command", std::string{});
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

inline void to_json(json& j, const Tab& t) {
    j = json{{"id", t.id.str()}, {"title", t.title}, {"pane_tree", pane_tree_to_json(t.pane_tree)}};
}

inline void from_json(const json& j, Tab& t) {
    t.id = TabId{j.value("id", std::string{})};
    t.title = j.value("title", std::string{});
    if (j.contains("pane_tree")) t.pane_tree = pane_tree_from_json(j.at("pane_tree"));
}

inline void to_json(json& j, const Window& w) {
    j = json{
        {"id", w.id.str()},
        {"title", w.title},
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
    w.title = j.value("title", std::string{});
    w.x = j.value("x", 0);
    w.y = j.value("y", 0);
    w.width = j.value("width", 0u);
    w.height = j.value("height", 0u);
    if (j.contains("tabs")) j.at("tabs").get_to(w.tabs);
    if (j.contains("focus_tab_id")) w.focus_tab_id = TabId{j.value("focus_tab_id", std::string{})};
    if (j.contains("focus_pane_id")) w.focus_pane_id = PaneId{j.value("focus_pane_id", std::string{})};
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
        {"tags", w.tags},
        {"windows", w.windows},
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
    if (j.contains("tags")) j.at("tags").get_to(w.tags);
    if (j.contains("windows")) j.at("windows").get_to(w.windows);
    if (j.contains("snapshots")) {
        w.snapshot_ids.clear();
        for (const auto& s : j.at("snapshots")) w.snapshot_ids.push_back(SnapshotId{s.get<std::string>()});
    }
    if (j.contains("focus_window_id")) w.focus_window_id = WindowId{j.value("focus_window_id", std::string{})};
}

} // namespace remin::core
