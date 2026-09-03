#include "terminal/pty/event_loop.hpp"

#include <poll.h>
#include <algorithm>

namespace remin::terminal {

EventLoop::EventLoop() = default;
EventLoop::~EventLoop() = default;

void EventLoop::add_fd(int fd, short events, IoCallback cb) {
    remove_fd(fd);
    entries_.push_back({fd, events, std::move(cb)});
}

void EventLoop::modify_fd(int fd, short events) {
    for (auto& e : entries_) {
        if (e.fd == fd) {
            e.events = events;
            return;
        }
    }
}

void EventLoop::remove_fd(int fd) {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [&](const Entry& e) { return e.fd == fd; }),
        entries_.end());
}

void EventLoop::run() {
    running_ = true;
    while (running_ && !entries_.empty()) {
        std::vector<struct pollfd> pfds;
        pfds.reserve(entries_.size());
        for (const auto& e : entries_) {
            pfds.push_back({e.fd, e.events, 0});
        }
        const int rc = poll(pfds.data(), static_cast<nfds_t>(pfds.size()), -1);
        if (rc < 0) {
            running_ = false;
            return;
        }
        if (rc == 0) continue;

        for (std::size_t i = 0; i < pfds.size() && running_; ++i) {
            if (pfds[i].revents == 0) continue;
            if (i < entries_.size()) {
                auto cb = entries_[i].cb;
                if (cb) cb(pfds[i].fd, pfds[i].revents);
            }
        }
    }
}

void EventLoop::stop() {
    running_ = false;
}

} // namespace remin::terminal
