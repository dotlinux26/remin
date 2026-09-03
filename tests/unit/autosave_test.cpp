#include "core/autosave.hpp"
#include "core/id.hpp"

#include <iostream>
#include <map>
#include <string>

using namespace remin::core;

// Minimal Storage fake: only scrollback writes are exercised here.
class FakeStorage : public Storage {
public:
    std::map<PaneId, std::string> scrollback;

    std::vector<Workspace> list_workspaces() override { return {}; }
    std::optional<Workspace> load_workspace(const WorkspaceId&) override { return std::nullopt; }
    void save_workspace(const Workspace&) override {}
    void delete_workspace(const WorkspaceId&) override {}
    std::vector<Snapshot> list_snapshots(const WorkspaceId&) override { return {}; }
    std::optional<json> load_snapshot(const WorkspaceId&, const SnapshotId&) override { return std::nullopt; }
    void save_snapshot(const WorkspaceId&, const Snapshot&, const json&) override {}
    void delete_snapshot(const WorkspaceId&, const SnapshotId&) override {}
    void store_scrollback(const PaneId& p, std::string c) override { scrollback[p] = std::move(c); }
    std::string load_scrollback(const PaneId&) override { return {}; }
};

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL: " #c " @ " << __LINE__ << "\n"; ++g_fail; } } while (0)

int main() {
    FakeStorage storage;
    Autosaver sa(&storage);

    // The provider is only called at flush time, never per keystroke.
    std::string live_text = "";
    sa.set_scrollback_provider([&](const PaneId&) -> std::optional<std::string> {
        return std::make_optional(live_text);
    });

    // Injectable clock: we advance it manually.
    std::chrono::steady_clock::time_point t{};
    sa.set_clock([&]() { return t; });
    sa.set_terminal_debounce(std::chrono::seconds(2));

    PaneId p = PaneId::generate();

    // Typing burst: note_activity is cheap (just inserts into a set).
    for (int i = 0; i < 5; ++i) {
        t += std::chrono::milliseconds(10);
        live_text = "line " + std::to_string(i);
        sa.note_terminal_activity(p);
        CHECK(!sa.flush());  // never flushes while activity keeps coming
    }
    CHECK(storage.scrollback.empty());  // provider not called yet

    // User stops typing: debounce not elapsed yet.
    t += std::chrono::seconds(1);
    CHECK(!sa.due());
    CHECK(!sa.flush());  // no write

    // Debounce window elapsed after the last keystroke.
    t += std::chrono::seconds(2);
    CHECK(sa.due());

    // Exactly ONE flush: provider is called, storage is written.
    CHECK(sa.flush());
    CHECK(storage.scrollback.at(p) == "line 4");  // latest snapshot only
    CHECK(!sa.due());

    // Idle: no further flush without new activity.
    t += std::chrono::seconds(60);
    CHECK(!sa.flush());
    CHECK(storage.scrollback.size() == 1);

    // New activity after quiet -> a fresh single flush.
    live_text = "line 100";
    sa.note_terminal_activity(p);
    t += std::chrono::seconds(3);
    CHECK(sa.flush());
    CHECK(storage.scrollback.at(p) == "line 100");

    // Multiple panes: only the one with activity gets flushed.
    PaneId p2 = PaneId::generate();
    live_text = "pane2 text";
    sa.note_terminal_activity(p2);
    t += std::chrono::seconds(3);
    CHECK(sa.flush());
    CHECK(storage.scrollback.count(p2) == 1);

    // --- Note policy: unified Autosaver, 10 s idle, note body provider. ---
    std::string note_body = "";
    sa.set_note_provider([&](const std::string&) -> std::optional<std::string> {
        return std::make_optional(note_body);
    });
    const std::string noteId = PaneId::generate().str();  // same id space
    note_body = "note v1";
    sa.note_note_activity(noteId);

    // Not due after 5 s (idle window is 10 s for notes).
    t += std::chrono::seconds(5);
    CHECK(!sa.due());
    CHECK(!sa.flush());

    // Due after the 10 s idle window elapses -> one note flush.
    t += std::chrono::seconds(6);
    CHECK(sa.due());
    CHECK(sa.flush());
    CHECK(storage.scrollback.count(PaneId(noteId)) == 1);
    CHECK(storage.scrollback.at(PaneId(noteId)) == "note v1");

    // flush_now persists immediately regardless of the idle window.
    note_body = "note v2";
    sa.note_note_activity(noteId);
    CHECK(sa.flush_now());
    CHECK(storage.scrollback.at(PaneId(noteId)) == "note v2");

    if (g_fail == 0) { std::cout << "autosave_test: OK\n"; return 0; }
    std::cerr << "autosave_test: " << g_fail << " failure(s)\n";
    return 1;
}
