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

    std::cout << "[PASS] EngineScriptProjectTests passed!\n";
    return 0;
}
