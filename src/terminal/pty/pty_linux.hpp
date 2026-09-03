#pragma once

#include "terminal/pty/pty_provider.hpp"

namespace remin::terminal {

// PTY provider backed by the POSIX forkpty() / Linux openpty().
class PtyProviderLinux : public PtyProvider {
public:
    std::unique_ptr<PtySession> spawn(const std::string& shell,
                                      const std::string& cwd,
                                      int cols, int rows,
                                      const std::vector<std::string>& env) override;
};

} // namespace remin::terminal
