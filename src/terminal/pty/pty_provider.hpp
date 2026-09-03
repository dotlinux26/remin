#pragma once

#include <string>
#include <vector>
#include <memory>

namespace remin::terminal {

// Result of spawning a PTY + shell.
struct PtySession {
    int master_fd{-1};   // PTY master; the app reads/writes here
    int child_pid{-1};   // shell process id
    std::string slave_name; // /dev/pts/N
};

// Abstraction over PTY creation so the core doesn't depend on a specific
// implementation (Linux forkpty today, remote/mock later).
class PtyProvider {
public:
    virtual ~PtyProvider() = default;

    // Spawn a shell inside a new PTY. Returns nullptr on failure.
    virtual std::unique_ptr<PtySession> spawn(const std::string& shell,
                                              const std::string& cwd,
                                              int cols = 80,
                                              int rows = 24,
                                              const std::vector<std::string>& env = {}) = 0;
};

} // namespace remin::terminal
