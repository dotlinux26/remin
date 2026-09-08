#include "gui/terminal/pane_history_tracker.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace remin::gui;

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL: " << #cond << " at " << __LINE__ << "\n";      \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        if ((a) != (b)) {                                                      \
            std::cerr << "FAIL: " << #a " == " #b << " (" << (a) << " != " << (b) << ") at " << __LINE__ << "\n"; \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static std::string temp_histfile() {
    static int counter = 0;
    return "/tmp/remin_hist_test_" + std::to_string(++counter) + ".hist";
}

static void write_histfile(const std::string& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::trunc);
    ofs << content;
}

int main() {
    // Test 1: Initial load from plain format
    {
        std::string path = temp_histfile();
        write_histfile(path, "ls\npwd\necho hello\n");

        PaneHistoryTracker tracker(path, "pane-1");
        tracker.initial_load();

        auto cmds = tracker.commands_desc();
        CHECK_EQ(cmds.size(), 3);
        CHECK_EQ(cmds[0].command, "echo hello"); // newest first
        CHECK_EQ(cmds[1].command, "pwd");
        CHECK_EQ(cmds[2].command, "ls");
        std::filesystem::remove(path);
    }

    // Test 2: Empty history file
    {
        std::string path = temp_histfile();
        write_histfile(path, "");

        PaneHistoryTracker tracker(path, "pane-2");
        tracker.initial_load();

        auto cmds = tracker.commands_desc();
        CHECK_EQ(cmds.size(), 0);
        std::filesystem::remove(path);
    }

    // Test 3: Non-existent history file
    {
        std::string path = "/tmp/nonexistent_histfile_" + std::to_string(getpid()) + ".hist";

        PaneHistoryTracker tracker(path, "pane-3");
        tracker.initial_load();

        auto cmds = tracker.commands_desc();
        CHECK_EQ(cmds.size(), 0);
    }

    // Test 4: Sync appends new commands
    {
        std::string path = temp_histfile();
        write_histfile(path, "ls\npwd\n");

        PaneHistoryTracker tracker(path, "pane-4");
        tracker.initial_load();
        CHECK_EQ(tracker.commands_desc().size(), 2);

        // Append new command
        write_histfile(path, "ls\npwd\necho new\n");
        tracker.sync();

        auto cmds = tracker.commands_desc();
        CHECK_EQ(cmds.size(), 3);
        CHECK_EQ(cmds[0].command, "echo new");
        std::filesystem::remove(path);
    }

    // Test 5: Repeated sync does not duplicate
    {
        std::string path = temp_histfile();
        write_histfile(path, "cmd1\ncmd2\n");

        PaneHistoryTracker tracker(path, "pane-5");
        tracker.initial_load();
        tracker.sync(); // no changes
        tracker.sync(); // no changes

        auto cmds = tracker.commands_desc();
        CHECK_EQ(cmds.size(), 2);
        std::filesystem::remove(path);
    }

    // Test 6: Adjacent duplicate dedupe on sync
    {
        std::string path = temp_histfile();
        write_histfile(path, "cmd1\n");

        PaneHistoryTracker tracker(path, "pane-6");
        tracker.initial_load();
        CHECK_EQ(tracker.commands_desc().size(), 1);

        // Append duplicate
        write_histfile(path, "cmd1\ncmd1\n");
        tracker.sync();

        auto cmds = tracker.commands_desc();
        CHECK_EQ(cmds.size(), 1);
        CHECK_EQ(cmds[0].command, "cmd1");
        std::filesystem::remove(path);
    }

    // Test 7: Non-adjacent duplicate is kept
    {
        std::string path = temp_histfile();
        write_histfile(path, "cmd1\ncmd2\n");

        PaneHistoryTracker tracker(path, "pane-7");
        tracker.initial_load();
        CHECK_EQ(tracker.commands_desc().size(), 2);

        // Append non-adjacent duplicate
        write_histfile(path, "cmd1\ncmd2\ncmd1\n");
        tracker.sync();

        auto cmds = tracker.commands_desc();
        CHECK_EQ(cmds.size(), 3);
        CHECK_EQ(cmds[0].command, "cmd1"); // newest
        CHECK_EQ(cmds[1].command, "cmd2");
        CHECK_EQ(cmds[2].command, "cmd1"); // oldest
        std::filesystem::remove(path);
    }

    // Test 8: HISTFILE truncation triggers full reload
    {
        std::string path = temp_histfile();
        write_histfile(path, "cmd1\ncmd2\ncmd3\n");

        PaneHistoryTracker tracker(path, "pane-8");
        tracker.initial_load();
        CHECK_EQ(tracker.commands_desc().size(), 3);

        // Truncate file (simulate history -c or manual edit)
        write_histfile(path, "new1\nnew2\n");
        tracker.sync();

        auto cmds = tracker.commands_desc();
        CHECK_EQ(cmds.size(), 2);
        CHECK_EQ(cmds[0].command, "new2");
        CHECK_EQ(cmds[1].command, "new1");
        std::filesystem::remove(path);
    }

    // Test 9: Pin command
    {
        std::string path = temp_histfile();
        write_histfile(path, "ls\npwd\ngit status\n");

        PaneHistoryTracker tracker(path, "pane-9");
        tracker.initial_load();

        tracker.pin_command("git status", true);
        CHECK(tracker.is_pinned("git status"));
        CHECK(!tracker.is_pinned("ls"));

        auto cmds = tracker.commands_desc();
        // Find git status
        bool found_pinned = false;
        for (const auto& c : cmds) {
            if (c.command == "git status" && c.pinned) {
                found_pinned = true;
                break;
            }
        }
        CHECK(found_pinned);

        // Unpin
        tracker.pin_command("git status", false);
        CHECK(!tracker.is_pinned("git status"));
        std::filesystem::remove(path);
    }

    // Test 10: Pin carries through sync
    {
        std::string path = temp_histfile();
        write_histfile(path, "cmd1\ncmd2\n");

        PaneHistoryTracker tracker(path, "pane-10");
        tracker.initial_load();
        tracker.pin_command("cmd2", true);

        // Append new command
        write_histfile(path, "cmd1\ncmd2\ncmd3\n");
        tracker.sync();

        CHECK(tracker.is_pinned("cmd2"));
        auto cmds = tracker.commands_desc();
        for (const auto& c : cmds) {
            if (c.command == "cmd2" && c.pinned) {
                CHECK(c.pinned);
            }
        }
        std::filesystem::remove(path);
    }

    // Test 11: Pin survives file truncation/reload
    {
        std::string path = temp_histfile();
        write_histfile(path, "cmd1\ncmd2\n");

        PaneHistoryTracker tracker(path, "pane-11");
        tracker.initial_load();
        tracker.pin_command("cmd1", true);

        // Truncate and add new
        write_histfile(path, "cmd1\ncmd3\n");
        tracker.sync();

        CHECK(tracker.is_pinned("cmd1"));
        std::filesystem::remove(path);
    }

    // Test 12: pane isolation - different trackers on different files
    {
        std::string path1 = temp_histfile();
        std::string path2 = temp_histfile();
        write_histfile(path1, "pane1-cmd1\n");
        write_histfile(path2, "pane2-cmd1\n");

        PaneHistoryTracker tracker1(path1, "pane-1");
        PaneHistoryTracker tracker2(path2, "pane-2");

        tracker1.initial_load();
        tracker2.initial_load();

        CHECK_EQ(tracker1.commands_desc()[0].command, "pane1-cmd1");
        CHECK_EQ(tracker2.commands_desc()[0].command, "pane2-cmd1");
        CHECK(tracker1.histfile_path() != tracker2.histfile_path());
        std::filesystem::remove(path1);
        std::filesystem::remove(path2);
    }

    // Test 13: commands_asc returns chronological order
    {
        std::string path = temp_histfile();
        write_histfile(path, "first\nsecond\nthird\n");

        PaneHistoryTracker tracker(path, "pane-13");
        tracker.initial_load();

        auto asc = tracker.commands_asc();
        CHECK_EQ(asc.size(), 3);
        CHECK_EQ(asc[0].command, "first");
        CHECK_EQ(asc[1].command, "second");
        CHECK_EQ(asc[2].command, "third");

        auto desc = tracker.commands_desc();
        CHECK_EQ(desc[0].command, "third");
        CHECK_EQ(desc[1].command, "second");
        CHECK_EQ(desc[2].command, "first");
        std::filesystem::remove(path);
    }

    // Test 14: Timestamped format parsing
    {
        std::string path = temp_histfile();
        // Bash timestamped format: #unix_timestamp\ncommand
        write_histfile(path, "#1700000000\nls\n#1700000001\npwd\n#1700000002\necho test\n");

        PaneHistoryTracker tracker(path, "pane-14");
        tracker.initial_load();

        auto cmds = tracker.commands_desc();
        CHECK_EQ(cmds.size(), 3);
        CHECK_EQ(cmds[0].command, "echo test");
        CHECK_EQ(cmds[0].timestamp_us, 1700000002LL * 1'000'000);
        CHECK_EQ(cmds[1].command, "pwd");
        CHECK_EQ(cmds[1].timestamp_us, 1700000001LL * 1'000'000);
        CHECK_EQ(cmds[2].command, "ls");
        CHECK_EQ(cmds[2].timestamp_us, 1700000000LL * 1'000'000);
        std::filesystem::remove(path);
    }

    if (g_failures == 0) {
        std::cout << "pane_history_tracker_test: OK\n";
        return 0;
    }
    std::cerr << "pane_history_tracker_test: " << g_failures << " failure(s)\n";
    return 1;
}