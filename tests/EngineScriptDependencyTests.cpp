#include <cassert>
#include <iostream>
#include "enginescript/depgraph/DependencyGraph.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptDependencyTests...\n";

    DependencyGraph graph;

    // A -> B -> C (A depends on B, B depends on C)
    graph.add_edge("A.e2s", "B.e2s", EdgeKind::Import);
    graph.add_edge("B.e2s", "C.e2s", EdgeKind::Import);

    // Test 1: Topological order (C must build before B, B before A)
    auto order = graph.topological_sort();
    assert(order.size() == 3);
    assert(order[0] == "C.e2s");
    assert(order[1] == "B.e2s");
    assert(order[2] == "A.e2s");

    // Test 2: Cycle detection
    DependencyGraph cyclic_graph;
    cyclic_graph.add_edge("A.e2s", "B.e2s", EdgeKind::Import);
    cyclic_graph.add_edge("B.e2s", "A.e2s", EdgeKind::Import);
    std::vector<std::string> cycle;
    assert(cyclic_graph.has_cycle(&cycle));
    assert(cycle.size() >= 2);

    // Test 3: Invalidation closure
    // Private body edit in B -> only B is invalidated
    auto priv_closure = graph.get_invalidation_closure("B.e2s", false);
    assert(priv_closure.size() == 1);
    assert(priv_closure.count("B.e2s") == 1);

    // Public signature edit in C -> B and A are invalidated!
    auto pub_closure = graph.get_invalidation_closure("C.e2s", true);
    assert(pub_closure.size() == 3);
    assert(pub_closure.count("C.e2s") == 1);
    assert(pub_closure.count("B.e2s") == 1);
    assert(pub_closure.count("A.e2s") == 1);

    std::cout << "[PASS] EngineScriptDependencyTests passed!\n";
    return 0;
}
