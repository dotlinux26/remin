#pragma once

#include <string>
#include <optional>

namespace remin::ipc {

// Client used by the CLI to talk to a running `remin gui` process
// over a Unix domain socket. If the server isn't running, the CLI should
// fall back to driving WorkspaceCore directly (headless mode).
class IpcClient {
public:
    // Send a request line, await one response line.
    // Returns std::nullopt if the server is unreachable.
    std::optional<std::string> request(const std::string& request_json);

    bool server_running(const std::string& path);
};

} // namespace remin::ipc
