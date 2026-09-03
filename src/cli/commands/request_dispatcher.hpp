#pragma once

#include "core/workspace_core.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace remin::cli {

// Maps a JSON-RPC-ish request { "method", "params" } to a WorkspaceCore
// call and returns a JSON response. Used both over IPC (GUI server) and
// directly by the CLI in headless mode — the frontends stay symmetric.
//
// Response shape:
//   success: { "ok": true,  "result": {...} }
//   error:   { "ok": false, "error": "..." }
class RequestDispatcher {
public:
    explicit RequestDispatcher(remin::core::WorkspaceCore* core) : core_(core) {}

    [[nodiscard]] std::string handle(const std::string& request_json);

private:
    remin::core::WorkspaceCore* core_;

    remin::core::json dispatch(const remin::core::json& req);
};

} // namespace remin::cli
