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
    assert(catalog.find_available_function("toString") == nullptr);
    assert(catalog.find_available_method("Events", "subscribe") == nullptr);
    assert(catalog.find_available_method("Events", "next") != nullptr);
    assert(catalog.find_available_method("Assets", "request") == nullptr);
    assert(catalog.find_available_method("Assets", "load") != nullptr);
    assert(catalog.find_available_method("Jobs", "run") != nullptr);
    const ApiMethodDescriptor* ignore_one_way = catalog.find_available_method("RigidBody2D", "setIgnoreOneWay");
    assert(ignore_one_way != nullptr);
    assert(ignore_one_way->parameters.size() == 1);
    assert(ignore_one_way->parameters[0].type == "Bool");
    const ApiMethodDescriptor* play_sound = catalog.find_available_method("Audio", "playSound");
    assert(play_sound != nullptr);
    assert(play_sound->return_type == "Bool");
    assert(play_sound->min_arguments == 1);
    assert(play_sound->max_arguments == 3);
    const ApiMethodDescriptor* load_asset = catalog.find_available_method("Assets", "load");
    assert(load_asset != nullptr);
    assert(load_asset->return_type == "Task<Result<Asset<T>, String>>");
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
    const ApiMethodDescriptor* find_gameplay_tag =
        catalog.find_available_method("Entities", "findByGameplayTag");
    assert(find_gameplay_tag != nullptr);
    assert(find_gameplay_tag->return_type == "Entity");
    assert(find_gameplay_tag->parameters.size() == 1);
    assert(find_gameplay_tag->parameters[0].type == "String");
    assert(catalog.find_available_method("Entities", "findAllByGameplayTag") != nullptr);
    assert(catalog.find_available_method("Entities", "hasGameplayTag") != nullptr);
    assert(catalog.find_available_method("Entities", "addGameplayTag") != nullptr);
    assert(catalog.find_available_method("Entities", "removeGameplayTag") != nullptr);
    const ApiMethodDescriptor* gameplay_tags = catalog.find_available_method("Entities", "gameplayTags");
    assert(gameplay_tags != nullptr);
    assert(gameplay_tags->return_type == "Array<String>");

    std::cout << "[PASS] EngineScriptApiCatalogTests passed!\n";
    return 0;
}
