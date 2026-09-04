#pragma once

#include <string>

namespace remin::core {

// Centralized persistence policy configuration (decision D-7).
//
// Three independent, explicit policies that were previously scattered as
// stray booleans across MainWindow / SessionController. Exposing them as one
// config object means the GUI, CLI and IPC all read/write persistence behavior
// through a single model instead of ad-hoc flags.

struct PersistencePolicy {
    // Default Recovery: restore the latest live-workspace checkpoint on the
    // next launch (shutdown/restart). Distinct from Window History (D-3).
    bool default_recovery = true;

    // Window History: keep snapshots of intentionally-closed windows so they
    // can be reopened (Ctrl+H, label + timestamp). When disabled, closing a
    // window only clears it from the live workspace without archiving a
    // recoverable snapshot.
    bool closed_window_history = true;

    // Auto-save by Input: persist workspace/note state as the user types
    // (debounced). When disabled, only explicit save / shutdown checkpoints
    // write.
    bool input_checkpoint = true;

    // Serialization helpers (nlohmann/json style).
    [[nodiscard]] std::string to_string() const;
    static PersistencePolicy from_string(const std::string& json);
};

} // namespace remin::core
