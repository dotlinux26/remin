#include "terminal/shell/shell.hpp"

#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace remin::terminal {

std::string detect_default_shell() {
    if (const char* sh = std::getenv("SHELL"); sh && *sh) {
        return sh;
    }
    if (const auto pw = getpwuid(getuid()); pw && pw->pw_shell && *pw->pw_shell) {
        return pw->pw_shell;
    }
    return "/bin/sh";
}

} // namespace remin::terminal
