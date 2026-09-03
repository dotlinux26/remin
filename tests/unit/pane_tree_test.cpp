#include "core/pane/pane.hpp"
#include "core/serialization.hpp"

#include <iostream>
#include <vector>
#include <cmath>

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
    // Build a nested pane tree: H(0.5){P1, V(0.6){P2, P3}}
    Pane p1{PaneId::generate(), PaneState{}};
    Pane p2{PaneId::generate(), PaneState{}};
    Pane p3{PaneId::generate(), PaneState{}};

    auto tree = PaneTree::split(
        PaneTree::Kind::SplitHorizontal,
        PaneTree::leaf(std::move(p1)),
        PaneTree::split(
            PaneTree::Kind::SplitVertical,
            PaneTree::leaf(std::move(p2)),
            PaneTree::leaf(std::move(p3)),
            0.6),
        0.5);

    // Collect leaves.
    std::vector<const Pane*> leaves;
    tree.collect_panes(leaves);
    CHECK(leaves.size() == 3);

    // Round-trip through JSON preserves structure + ratios.
    auto j = pane_tree_to_json(tree);
    auto restored = pane_tree_from_json(j);

    std::vector<const Pane*> restored_leaves;
    restored.collect_panes(restored_leaves);
    CHECK(restored_leaves.size() == 3);
    CHECK(restored.kind() == PaneTree::Kind::SplitHorizontal);

    // Nested split ratio preserved.
    CHECK(restored.second() != nullptr);
    CHECK(restored.second()->kind() == PaneTree::Kind::SplitVertical);
    CHECK(std::abs(restored.second()->ratio() - 0.6) < 1e-9);

    if (g_failures == 0) {
        std::cout << "pane_tree_test: OK\n";
        return 0;
    }
    std::cerr << "pane_tree_test: " << g_failures << " failure(s)\n";
    return 1;
}
