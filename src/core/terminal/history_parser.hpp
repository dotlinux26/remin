#pragma once

#include "core/pane/pane.hpp"

#include <string>
#include <vector>
#include <optional>

namespace remin::core::terminal {

// Parse a Bash history file into CommandRecord entries.
// Supports both plain format (one command per line) and timestamped format
// (#timestamp\ncommand). Tolerates empty lines and malformed entries.
[[nodiscard]] std::vector<CommandRecord> parse_history_file(const std::string& path);

// Parse history from a string buffer (useful for testing).
// Returns all successfully parsed entries in file order (oldest first).
[[nodiscard]] std::vector<CommandRecord> parse_history_buffer(const std::string& content);

} // namespace remin::core::terminal