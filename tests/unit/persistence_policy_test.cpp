#include "core/persistence_policy.hpp"

#include <iostream>
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL: " << #cond << " at " << __LINE__ << "\n";      \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

using namespace remin::core;

int main() {
    // Defaults.
    PersistencePolicy p;
    CHECK(p.default_recovery == true);
    CHECK(p.closed_window_history == true);
    CHECK(p.input_checkpoint == true);

    // Round-trip through the compact string form.
    p.default_recovery = true;
    p.closed_window_history = false;
    p.input_checkpoint = true;
    auto s = p.to_string();
    CHECK(!s.empty());

    auto back = PersistencePolicy::from_string(s);
    CHECK(back.default_recovery == true);
    CHECK(back.closed_window_history == false);
    CHECK(back.input_checkpoint == true);

    // Empty / malformed input falls back to defaults.
    auto dflt = PersistencePolicy::from_string("");
    CHECK(dflt.default_recovery == true);
    CHECK(dflt.closed_window_history == true);
    auto bad = PersistencePolicy::from_string("{ not valid json ");
    CHECK(bad.default_recovery == true);

    // Explicit overrides survive.
    PersistencePolicy q2;
    q2.input_checkpoint = false;
    auto q2s = q2.to_string();
    auto q2r = PersistencePolicy::from_string(q2s);
    CHECK(q2r.input_checkpoint == false);

    if (g_failures == 0) {
        std::cout << "persistence_policy_test: OK\n";
        return 0;
    }
    std::cerr << "persistence_policy_test: " << g_failures << " failure(s)\n";
    return 1;
}
