#include "core/id.hpp"
#include "core/serialization.hpp"

#include <iostream>
#include <cstdlib>

static int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL: " << #cond << " at " << __LINE__ << "\n";  \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

using namespace remin::core;

int main() {
    // Id uniqueness + round-trip.
    auto w1 = WorkspaceId::generate();
    auto w2 = WorkspaceId::generate();
    CHECK(w1 != w2);
    CHECK(WorkspaceId{w1.str()} == w1);

    // ISO8601 round-trip.
    auto now = std::chrono::system_clock::now();
    auto s = to_iso8601(now);
    auto back = from_iso8601(s);
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - back).count();
    CHECK(std::abs(delta) <= 1);

    if (g_failures == 0) {
        std::cout << "serialization_test: OK\n";
        return 0;
    }
    std::cerr << "serialization_test: " << g_failures << " failure(s)\n";
    return 1;
}
