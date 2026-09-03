#include "terminal/pty/pty_linux.hpp"

#include <pty.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <cstring>
#include <cstdlib>

namespace remin::terminal {

std::unique_ptr<PtySession> PtyProviderLinux::spawn(const std::string& shell,
                                                    const std::string& cwd,
                                                    int cols, int rows,
                                                    const std::vector<std::string>& env) {
    // Set up window size so the child sees the right geometry.
    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    int master = -1;
    const auto shell_cstr = shell.empty() ? "/bin/sh" : shell.c_str();

    // forkpty creates the pty AND forks in one call.
    const int pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid == -1) return nullptr;

    if (pid == 0) {
        // Child: exec the shell.
        if (!cwd.empty()) {
            // best-effort; the shell may still start in / if chdir fails
            const int rc = chdir(cwd.c_str());
            (void)rc;
        }
        // Set TERM for our managed terminal (Remin owns the terminal).
        setenv("TERM", "xterm-256color", 1);

        // Apply user-supplied env overrides.
        for (const auto& kv : env) {
            const auto eq = kv.find('=');
            if (eq != std::string::npos) {
                setenv(kv.substr(0, eq).c_str(), kv.substr(eq + 1).c_str(), 1);
            }
        }

        char* const argv[] = { const_cast<char*>(shell_cstr), nullptr };
        char* const envp[] = { nullptr };
        execvpe(shell_cstr, argv, envp);
        _exit(127);
    }

    // Parent.
    auto sess = std::make_unique<PtySession>();
    sess->master_fd = master;
    sess->child_pid = pid;
    sess->slave_name = ptsname(master) ? ptsname(master) : "";

    // Ensure master fd is non-blocking for the poll() event loop.
    const int flags = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);

    return sess;
}

} // namespace remin::terminal
