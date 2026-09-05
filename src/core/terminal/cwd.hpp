#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace remin::core::terminal {

// Pure helpers for capturing/restoring a pane's working directory.
// Design §4 ("Pane CWD — shell context cwd"):
//   Capture order:  OSC 7 URI → /proc/<shell_pid>/cwd → cached value → $HOME.
//   Restore:        only spawn where the directory still exists; else $HOME.

namespace detail {

inline int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decode %XX escapes (OSC 7 encodes the path percent-escaped).
inline std::string percent_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hex_value(s[i + 1]);
            const int lo = hex_value(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

} // namespace detail

// Convert an OSC 7 "file://" URI (as reported by the shell) to a filesystem
// path. Returns an empty string when the URI is not a *local* file URI:
// remote authorities ("file://host/path") are meaningless as a shell cwd.
[[nodiscard]] inline std::string osc7_file_uri_to_path(std::string_view uri) {
    constexpr std::string_view kPrefix = "file://";
    if (!uri.starts_with(kPrefix)) return {};
    std::string_view rest = uri.substr(kPrefix.size());
    const std::size_t slash = rest.find('/');
    if (slash == std::string_view::npos) return {};
    // Empty authority ("" before the first '/') means local:
    //   "file:///home/user"  → authority "" , path "/home/user"
    //   "file://localhost/x" → not handled in V1 (rare); treat as remote.
    if (detail::percent_decode(rest.substr(0, slash)) != "") return {};
    return detail::percent_decode(rest.substr(slash));
}

// Read /proc/<pid>/cwd — the working directory of the pane's shell process
// (interactive `cd` changes it). Returns "" for any invalid/missing pid.
[[nodiscard]] inline std::string read_proc_cwd(long pid) {
    if (pid <= 0) return {};
    std::error_code ec;
    auto p = std::filesystem::canonical("/proc/" + std::to_string(pid) + "/cwd", ec);
    if (ec) return {};
    return p.string();
}

// Capture-side order (design §4.2): OSC 7 wins, then the shell's /proc cwd,
// then the last cached value, then $HOME.
[[nodiscard]] inline std::string pick_capture_cwd(const std::string& osc7_path,
                                                  const std::string& proc_cwd,
                                                  const std::string& cached,
                                                  const std::string& home) {
    if (!osc7_path.empty()) return osc7_path;
    if (!proc_cwd.empty()) return proc_cwd;
    if (!cached.empty()) return cached;
    return home;
}

// Restore-side decision (design §4.2): only spawn where the captured directory
// still exists (may be unmounted at restore time); fall back to $HOME.
// Returns "" so the caller can spawn with the shell's own default.
[[nodiscard]] inline std::string pick_restore_cwd(const std::string& captured,
                                                  const std::string& home) {
    if (!captured.empty() && std::filesystem::is_directory(captured)) return captured;
    if (!home.empty() && std::filesystem::is_directory(home)) return home;
    return {};
}

} // namespace remin::core::terminal