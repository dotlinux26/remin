#pragma once

#include "core/workspace_core.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace remin::core {

// In-process, edge-triggered autosave (ADR-0006: no daemon).
//
// Remin does NOT autosave on a blind periodic timer. Flushing is *triggered by
// activity*: the host calls note_terminal_activity()/note_note_activity() (e.g.
// on each keystroke or note buffer change). The scheduler then debounces — it
// flushes a resource exactly once a delay after that resource's *last*
// activity, and not again until new activity arrives.
//
// There is ONE Autosaver (owned by WorkspaceSession, never by a widget), but the
// save policy differs by resource kind so terminals and notes share the same
// pipeline without a second, parallel autosave mechanism:
//
//   Terminal  note_terminal_activity -> debounce (default 2 s)
//   Note      note_note_activity     -> idle     (default 10 s)
//   (Explicit && flush-immediately and Shutdown both call flush_now())
//
// Payload capture is lazy and is supplied by per-kind providers, so a quiet
// resource never writes to disk; only active editing does, and only once per
// burst.
class Autosaver {
public:
    enum class Kind {
        Terminal,  // payload = scrollback text captured via scrollback provider
        Note,      // payload = note body captured via note provider
    };

    explicit Autosaver(Storage* storage);
    ~Autosaver();

    // Host-provided readers, called only at flush time (never per keystroke).
    using ScrollbackProvider =
        std::function<std::optional<std::string>(const PaneId&)>;
    using NoteProvider =
        std::function<std::optional<std::string>(const std::string& noteId)>;
    void set_scrollback_provider(ScrollbackProvider p) { scrollback_provider_ = std::move(p); }
    void set_note_provider(NoteProvider p) { note_provider_ = std::move(p); }

    // Mark activity. Cheap and safe to call repeatedly; (re)ticks that resource.
    void note_terminal_activity(const PaneId& pane);
    void note_note_activity(const std::string& noteId);

    // Per-kind policy thresholds.
    void set_terminal_debounce(std::chrono::milliseconds d) { terminal_debounce_ = d; }
    void set_note_idle(std::chrono::milliseconds d) { note_idle_ = d; }

    // True when at least one pending resource is due (its policy window has
    // elapsed since its last activity), so the host should call flush().
    [[nodiscard]] bool due() const;

    // Flush every pending resource that is due (writes it and drops it from the
    // set). Returns true if anything was written.
    bool flush();

    // Unconditionally persist every pending resource now (explicit save /
    // shutdown), ignoring the debounce window.
    bool flush_now();

    // Testability / clock injection.
    using Clock = std::chrono::steady_clock;
    void set_clock(std::function<Clock::time_point()> fn) { now_ = std::move(fn); }

private:
    struct Entry {
        Kind kind;
        Clock::time_point last;  // last activity time
    };

    bool due_entry(const Entry& e) const;
    bool write_entry(const std::string& id, const Entry& e);

    Storage* storage_;
    ScrollbackProvider scrollback_provider_;
    NoteProvider note_provider_;
    // Keyed by pane/note id. PaneId and NoteId share the same globally-unique
    // Id generator, so ids never collide across kinds.
    std::unordered_map<std::string, Entry> pending_;
    std::function<Clock::time_point()> now_{[]() { return Clock::now(); }};
    std::chrono::milliseconds terminal_debounce_{std::chrono::seconds(2)};
    std::chrono::milliseconds note_idle_{std::chrono::seconds(10)};
};

} // namespace remin::core
