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
    assert(catalog.has_available_type("Particles"));
    assert(catalog.has_available_type("ParticleEffect2D"));
    assert(catalog.has_available_type("ParticleEmitOptions"));
    assert(catalog.find_available_type("ParticleEmitOptions")->value_type);
    assert(catalog.has_available_function("delay"));
    assert(catalog.has_available_function("instantiate"));
    assert(catalog.has_available_function("findEntities"));
    const ApiFunctionDescriptor* to_string = catalog.find_available_function("toString");
    assert(to_string != nullptr);
    assert(to_string->return_type == "String");
    assert(to_string->parameters.size() == 1);
    const ApiMethodDescriptor* ignore_one_way = catalog.find_available_method("RigidBody2D", "setIgnoreOneWay");
    assert(ignore_one_way != nullptr);
    assert(ignore_one_way->parameters.size() == 1);
    assert(ignore_one_way->parameters[0].type == "Bool");
    const ApiMethodDescriptor* play_sound = catalog.find_available_method("Audio", "playSound");
    assert(play_sound != nullptr);
    assert(play_sound->return_type == "Bool");
    assert(play_sound->min_arguments == 1);
    assert(play_sound->max_arguments == 3);
    const ApiMethodDescriptor* request_asset = catalog.find_available_method("Assets", "request");
    assert(request_asset != nullptr);
    assert(request_asset->return_type == "Bool");
    const ApiMethodDescriptor* scene_loading = catalog.find_available_method("Scene", "isLoading");
    assert(scene_loading != nullptr);
    assert(scene_loading->return_type == "Bool");
    const ApiMethodDescriptor* emit_particles = catalog.find_available_method("Particles", "emit");
    assert(emit_particles != nullptr);
    assert(emit_particles->return_type == "Bool");
    assert(emit_particles->min_arguments == 2);
    assert(emit_particles->max_arguments == 3);
    const ApiMethodDescriptor* stop_emitter = catalog.find_available_method("ParticleEmitter2D", "stop");
    assert(stop_emitter != nullptr);
    assert(stop_emitter->min_arguments == 0);
    assert(stop_emitter->max_arguments == 1);
    assert(catalog.find_available_method("ParticleEmitter2D", "setEffect") != nullptr);
    assert(catalog.find_available_property("ParticleEmitter2D", "effectHandle") != nullptr);
    const ApiPropertyDescriptor* particle_scale = catalog.find_available_property("ParticleEmitOptions", "scale");
    assert(particle_scale != nullptr && particle_scale->writable);

    std::cout << "[PASS] EngineScriptApiCatalogTests passed!\n";
    return 0;
}
