#pragma once

#include <string>
#include <functional>
#include <memory>

namespace remin::ipc {

// JSON-RPC-ish request { "method": "...", "params": {...}, "id": N }
using RequestHandler = std::function<std::string(const std::string& request_json)>;

struct SocketPath {
    static std::string default_path();
};

// Unix domain socket server (hosted inside the GUI process).
// Each request is a line-delimited JSON message; response is one JSON line.
class IpcServer {
public:
    explicit IpcServer(std::string path);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start();
    void stop();
    void set_handler(RequestHandler h) { handler_ = std::move(h); }

    // The listening fd (for use with the app's event loop).
    int listen_fd() const { return listen_fd_; }

private:
    std::string path_;
    int listen_fd_{-1};
    RequestHandler handler_;
};

} // namespace remin::ipc
