#include "ipc/client/ipc_client.hpp"
#include "ipc/server/ipc_server.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <algorithm>

namespace remin::ipc {

bool IpcClient::server_running(const std::string& path) {
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    const bool ok = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return ok;
}

std::optional<std::string> IpcClient::request(const std::string& request_json) {
    const auto path = SocketPath::default_path();
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return std::nullopt;

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return std::nullopt;
    }

    std::string out = request_json + "\n";
    const char* p = out.data();
    std::size_t remain = out.size();
    while (remain > 0) {
        const auto n = ::write(fd, p, remain);
        if (n <= 0) { ::close(fd); return std::nullopt; }
        p += n;
        remain -= static_cast<std::size_t>(n);
    }

    // Read until newline.
    std::string response;
    char buf[4096];
    while (true) {
        const auto n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        response.append(buf, static_cast<std::size_t>(n));
        if (response.find('\n') != std::string::npos) break;
    }
    ::close(fd);

    if (response.empty()) return std::nullopt;
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    return response;
}

} // namespace remin::ipc
