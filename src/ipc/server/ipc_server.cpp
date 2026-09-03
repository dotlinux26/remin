#include "ipc/server/ipc_server.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <stdexcept>

namespace remin::ipc {

std::string SocketPath::default_path() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    return std::string(runtime && *runtime ? runtime : "/tmp") +
           "/remin.sock";
}

IpcServer::IpcServer(std::string path) : path_(std::move(path)) {}
IpcServer::~IpcServer() { stop(); }

bool IpcServer::start() {
    unlink(path_.c_str());
    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (listen(listen_fd_, 8) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    return true;
}

void IpcServer::stop() {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    unlink(path_.c_str());
}

} // namespace remin::ipc
