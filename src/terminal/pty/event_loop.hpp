#pragma once

#include <poll.h>
#include <functional>
#include <vector>
#include <optional>

namespace remin::terminal {

// A read/write callback for a given fd.
using IoCallback = std::function<void(int fd, short revents)>;

// Small poll()-based event loop. Abstraction so we can later swap in epoll
// without touching the rest of the codebase.
//
// V1 uses poll(): Remin deals with a handful of PTY fds, not 10k sockets.
// Optimize to epoll only if profiling shows a need.
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // Register a descriptor. Callback invoked when events on fd are ready.
    void add_fd(int fd, short events, IoCallback cb);
    void modify_fd(int fd, short events);
    void remove_fd(int fd);

    // Run until stop() is called or the loop is empty.
    void run();
    void stop();

private:
    struct Entry {
        int fd;
        short events;
        IoCallback cb;
    };

    std::vector<Entry> entries_;
    bool running_{false};
};

} // namespace remin::terminal
