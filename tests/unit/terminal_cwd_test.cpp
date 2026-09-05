#include "core/terminal/cwd.hpp"

#include <filesystem>
#include <iostream>
#include <string>

// Minimal test harness (mirrors workspace_core_test.cpp).
static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL: " << #cond << " at " << __LINE__ << "\n";      \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

using namespace remin::core::terminal;

int main() {
    // -- osc7_file_uri_to_path --
    CHECK(osc7_file_uri_to_path("file:///home/user") == "/home/user");
    CHECK(osc7_file_uri_to_path("file:///tmp/a%20b") == "/tmp/a b");          // %20 → space
    CHECK(osc7_file_uri_to_path("file:///tmp/%C4%91") == "/tmp/\u0111");      // %C4%91 → đ
    CHECK(osc7_file_uri_to_path("file://localhost/x") == "");                 // remote host → unusable
    CHECK(osc7_file_uri_to_path("file://") == "");                            // no path
    CHECK(osc7_file_uri_to_path("http:///x") == "");                          // not file://
    CHECK(osc7_file_uri_to_path("") == "");

    // -- read_proc_cwd --
    CHECK(read_proc_cwd(0) == "");      // no pid → empty
    CHECK(read_proc_cwd(-5) == "");

    // -- pick_capture_cwd: OSC 7 wins, then /proc, then cached, then HOME --
    CHECK(pick_capture_cwd("/a", "/b", "/c", "/h") == "/a");
    CHECK(pick_capture_cwd("", "/b", "/c", "/h") == "/b");
    CHECK(pick_capture_cwd("", "", "/c", "/h") == "/c");
    CHECK(pick_capture_cwd("", "", "", "/h") == "/h");
    CHECK(pick_capture_cwd("", "", "", "") == "");

    // -- pick_restore_cwd: only spawn where the dir still exists --
    const std::string tmp = std::filesystem::temp_directory_path().string();
    CHECK(pick_restore_cwd(tmp, "/nonexistent-home") == tmp);    // existing wins
    CHECK(pick_restore_cwd("/definitely/not/exists", "") == ""); // nothing valid → ""
    CHECK(pick_restore_cwd("", "") == "");
    // Missing captured dir falls back to a valid $HOME.
    CHECK(pick_restore_cwd("/definitely/not/exists", "/") == "/");
    // Captured empty → falls to home.
    CHECK(pick_restore_cwd("", "/") == "/");

    if (g_failures == 0) {
        std::cout << "terminal_cwd_test: OK\n";
        return 0;
    }
    std::cerr << "terminal_cwd_test: " << g_failures << " failure(s)\n";
    return 1;
}