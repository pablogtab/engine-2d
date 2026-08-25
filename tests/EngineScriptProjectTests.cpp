#include <cassert>
#include <iostream>
#include "enginescript/editor/EngineScriptWorkspace.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptProjectTests...\n";

    EngineScriptWorkspace workspace("/workspace/MyGame");

    // File 1: Data/Item.e2s
    std::string item_code = R"(
        export struct ItemSlot {
            itemId: Int = 0;
            count: Int = 1;
        }
    )";
    workspace.add_source_file("Source/Data/Item.e2s", item_code);

    // File 2: Behaviours/Inventory.e2s
    std::string inv_code = R"(
        import { ItemSlot } from "../Data/Item";

        @behaviour
        export class InventoryHolder extends Behaviour {
            slot: ItemSlot;
        }
    )";
    workspace.add_source_file("Source/Behaviours/Inventory.e2s", inv_code);

    CompilationResult res = workspace.compile();
    assert(res.success);
    assert(res.error_count == 0);
    assert(res.generated_cpp.size() == 2);

    // Verify metadata manifest has both struct and behaviour
    const auto& manifest = workspace.metadata_manifest();
    assert(manifest.behaviours.size() == 1);
    assert(manifest.behaviours[0].name == "InventoryHolder");
    assert(manifest.structs.size() == 1);
    assert(manifest.structs[0] == "ItemSlot");

    // An unchanged workspace is served from the integrated compiler cache.
    const auto misses_after_first_compile = workspace.compiler_cache().stats().misses;
    CompilationResult cached = workspace.compile();
    assert(cached.success);
    assert(cached.generated_cpp.size() == 2);
    assert(workspace.compiler_cache().stats().hits >= 2);
    assert(workspace.compiler_cache().stats().misses == misses_after_first_compile);

    // Rebuilding the graph after an edit must not retain duplicate import edges.
    workspace.update_source_file("Source/Behaviours/Inventory.e2s", inv_code + "\n// implementation edit\n");
    CompilationResult rebuilt = workspace.compile();
    assert(rebuilt.success);
    assert(workspace.dependency_graph().edges().size() == 1);

    const auto hits_before_force = workspace.compiler_cache().stats().hits;
    CompilationResult forced = workspace.compile(true);
    assert(forced.success);
    assert(workspace.compiler_cache().stats().hits == hits_before_force);

    EngineScriptWorkspace growth_workspace("/workspace/GenericGrowth");
    growth_workspace.add_source_file("Source/Generics.e2s", R"(
        export function identity<T extends Send>(value: T): T { return value; }
        export function useGenerics(): Int {
            let values: Array<Int> = [1, 2];
            return identity<Int>(values[0]);
        }
    )");
    const CompilationResult growth = growth_workspace.compile();
    assert(growth.success);
    assert(growth.generic_code_growth.unique_instantiations >= 2);
    assert(growth.generic_code_growth.generated_cpp_bytes > 0);
    bool reported_array = false;
    bool reported_identity = false;
    for (const GenericInstantiation& instantiation : growth.generic_code_growth.instantiations) {
        reported_array |= instantiation.template_name == "Array" && instantiation.type_args == std::vector<std::string>{"Int"};
        reported_identity |= instantiation.template_name == "identity" && instantiation.type_args == std::vector<std::string>{"Int"};
    }
    assert(reported_array && reported_identity);

    std::cout << "[PASS] EngineScriptProjectTests passed!\n";
    return 0;
}
