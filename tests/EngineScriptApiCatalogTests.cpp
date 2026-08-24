#include "enginescript/api/ApiCatalog.hpp"
#include "enginescript/module/GameModule.hpp"

#include <cassert>
#include <iostream>

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptApiCatalogTests...\n";

    const ApiCatalog& catalog = ApiCatalog::instance();
    assert(catalog.abi_version() == ENGINE_SCRIPT_ABI_VERSION);
    assert(catalog.sdk_hash() == ENGINE_SCRIPT_SDK_HASH);
    assert(catalog.has_available_type("Input"));
    assert(catalog.has_available_type("SpriteRenderer2D"));
    assert(catalog.has_available_type("Audio"));
    assert(catalog.has_available_type("Scene"));
    assert(catalog.has_available_function("delay"));
    assert(!catalog.has_available_function("instantiate"));
    const ApiMethodDescriptor* play_sound = catalog.find_available_method("Audio", "playSound");
    assert(play_sound != nullptr);
    assert(play_sound->return_type == "Bool");
    assert(play_sound->min_arguments == 1);
    assert(play_sound->max_arguments == 3);

    std::cout << "[PASS] EngineScriptApiCatalogTests passed!\n";
    return 0;
}
