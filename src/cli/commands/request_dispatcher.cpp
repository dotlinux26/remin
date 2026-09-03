#include "cli/commands/request_dispatcher.hpp"
#include "core/serialization.hpp"

#include <stdexcept>

namespace remin::cli {

namespace core = remin::core;

std::string RequestDispatcher::handle(const std::string& request_json) {
    try {
        auto req = core::json::parse(request_json);
        auto resp = dispatch(req);
        return resp.dump();
    } catch (const std::exception& e) {
        return core::json({{"ok", false}, {"error", e.what()}}).dump();
    } catch (...) {
        return core::json({{"ok", false}, {"error", "unknown"}}).dump();
    }
}

core::json RequestDispatcher::dispatch(const core::json& req) {
    const auto method = req.value("method", std::string{});
    const auto params = req.value("params", core::json::object());

    if (method == "workspace.list") {
        auto ws = core_->current_workspace();
        core::json r;
        r["open"] = ws != nullptr;
        if (ws) r["name"] = ws->name;
        return {{"ok", true}, {"result", r}};
    }
    if (method == "workspace.create") {
        const std::string name = params.value("name", std::string{"untitled"});
        const auto id = core_->create_workspace(name);
        return {{"ok", true}, {"result", {{"workspace_id", id.str()}}}};
    }
    if (method == "workspace.open") {
        const auto id = params["workspace_id"].get<std::string>();
        const bool ok = core_->open_workspace(core::WorkspaceId{id});
        return {{"ok", ok}, {"result", {{"opened", ok}}}};
    }
    if (method == "workspace.close") {
        const bool ok = core_->close_workspace();
        return {{"ok", ok}};
    }
    if (method == "workspace.rename") {
        const auto id = params["workspace_id"].get<std::string>();
        const auto name = params["name"].get<std::string>();
        const bool ok = core_->rename_workspace(core::WorkspaceId{id}, name);
        return {{"ok", ok}};
    }
    if (method == "window.add") {
        const std::string title = params.value("title", std::string{"window"});
        const auto id = core_->add_window(title);
        return {{"ok", true}, {"result", {{"window_id", id.str()}}}};
    }
    if (method == "window.rename") {
        const auto id = params["window_id"].get<std::string>();
        const auto name = params["name"].get<std::string>();
        const bool ok = core_->rename_window(core::WindowId{id}, name);
        return {{"ok", ok}};
    }
    if (method == "snapshot.create") {
        const auto id = core_->create_snapshot();
        return {{"ok", true}, {"result", {{"snapshot_id", id.str()}}}};
    }
    return {{"ok", false}, {"error", "unknown method: " + method}};
}

} // namespace remin::cli
