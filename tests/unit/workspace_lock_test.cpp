#include "core/workspace_lock.hpp"
#include "core/id.hpp"

#include <unistd.h>
#include <filesystem>
#include <iostream>
#include <string>

using namespace remin::core;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL: " #c " @ " << __LINE__ << "\n"; ++g_fail; } } while (0)

int main() {
    const std::string dir = "/tmp/remin-test-locks-" + std::to_string(::getpid());
    const WorkspaceId ws = WorkspaceId::generate();
    const WorkspaceId ws2 = WorkspaceId::generate();

    WorkspaceLock a;
    WorkspaceLock b;

    // First lock acquires.
    CHECK(a.acquire(dir, ws));
    CHECK(a.held());
    CHECK(WorkspaceLock::is_locked(dir, ws));

    // Second lock on the same workspace is refused while the first is held.
    CHECK(!b.acquire(dir, ws));
    CHECK(!b.held());

    // A *different* workspace is not locked.
    CHECK(!WorkspaceLock::is_locked(dir, ws2));

    // Releasing allows a later acquisition.
    a.release();
    CHECK(!a.held());
    CHECK(!WorkspaceLock::is_locked(dir, ws));
    CHECK(b.acquire(dir, ws));
    CHECK(b.held());

    b.release();
    std::filesystem::remove_all(dir);

    if (g_fail == 0) { std::cout << "workspace_lock_test: OK\n"; return 0; }
    std::cerr << "workspace_lock_test: " << g_fail << " failure(s)\n";
    return 1;
}
