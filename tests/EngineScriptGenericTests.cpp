#include <cassert>
#include <iostream>
#include "enginescript/generics/Generics.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptGenericTests...\n";

    GenericsManager manager;

    // Test 1: Monomorphization name mangling
    std::string m1 = manager.request_instantiation("Array", {"ItemSlot"});
    assert(m1 == "Array_ItemSlot");

    std::string m2 = manager.request_instantiation("Map", {"String", "Float"});
    assert(m2 == "Map_String_Float");

    // Test 2: Instantiation reuse
    std::string m3 = manager.request_instantiation("Array", {"ItemSlot"});
    assert(m3 == m1);
    assert(manager.instantiation_count() == 2); // Reused Array<ItemSlot>, did not duplicate!

    // Test 3: Complex generic nesting
    std::string m4 = manager.request_instantiation("Result", {"Array<ItemSlot>", "String"});
    assert(m4.find("Result_Array_ItemSlot__String") != std::string::npos);
    assert(manager.instantiation_count() == 3);

    // Emission order is independent of request order and recursive expansion
    // reports a cycle before unbounded monomorphization.
    GenericsManager deterministic;
    deterministic.request_instantiation("Zed", {"Int"});
    deterministic.request_instantiation("Alpha", {"String"});
    assert(deterministic.all_instantiations().front().template_name == "Alpha");
    assert(deterministic.begin_instantiation("Node", {"Int"}));
    assert(!deterministic.begin_instantiation("Node", {"Int"}));
    deterministic.end_instantiation("Node", {"Int"});
    assert(deterministic.begin_instantiation("Node", {"Int"}));

    std::cout << "[PASS] EngineScriptGenericTests passed!\n";
    return 0;
}
