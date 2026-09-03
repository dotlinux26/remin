#pragma once

#include <string>

namespace remin::terminal {

// Determine which shell to spawn for a new pane.
// Prefers $SHELL, falls back to the user's passwd entry, then /bin/sh.
std::string detect_default_shell();

} // namespace remin::terminal
