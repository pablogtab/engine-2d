#include "enginescript/api/ApiCatalog.hpp"
#include "enginescript/module/GameModule.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptApiCatalogTests...\n";

    const ApiCatalog& catalog = ApiCatalog::instance();
    assert(catalog.abi_version() == ENGINE_SCRIPT_ABI_VERSION);
    assert(catalog.sdk_hash() == ENGINE_SCRIPT_SDK_HASH);
    assert(catalog.compiler_cache_id() == ENGINE_SCRIPT_COMPILER_CACHE_ID);
    assert(catalog.has_available_type("Math"));
    assert(catalog.has_available_type("Ease"));
    assert(catalog.has_available_type("Random"));
    assert(catalog.has_available_type("Rect"));
    assert(catalog.has_available_type("Bounds2"));
    assert(catalog.has_available_type("Noise"));
    const ApiMethodDescriptor* math_clamp = catalog.find_available_method("Math", "clamp");
    assert(math_clamp != nullptr);
    assert(math_clamp->return_type == "Float");
    assert(math_clamp->parameters.size() == 3);
    assert(catalog.find_available_property("Math", "PI") != nullptr);
    assert(catalog.find_available_method("Vec2", "moveTowards") != nullptr);
    assert(catalog.find_available_method("Vec2", "signedAngle") != nullptr);
    assert(catalog.find_available_method("Vec3", "clampMagnitude") != nullptr);
    assert(catalog.find_available_method("Vec4", "normalized") != nullptr);
    assert(catalog.find_available_method("Color", "withAlpha") != nullptr);
    assert(catalog.find_available_method("Ease", "inOutExpo") != nullptr);
    assert(catalog.find_available_method("Ease", "inOutBack") != nullptr);
    assert(catalog.find_available_method("Random", "insideUnitCircle") != nullptr);
    assert(catalog.find_available_method("Rect", "intersection") != nullptr);
    assert(catalog.find_available_method("Rect", "intersection")->nullable_return);
    assert(catalog.find_available_method("Bounds2", "intersects") != nullptr);
    assert(catalog.find_available_method("Noise", "fbm2D") != nullptr);
    assert(catalog.find_available_method("Color", "srgbToLinear") != nullptr);
    assert(catalog.find_available_method("Ease", "outElasticUnclamped") != nullptr);
    assert(VectorOps::clampMagnitude(glm::vec2{3.0f, 4.0f}, 2.0f) == glm::vec2(1.2f, 1.6f));
    assert(std::abs(VectorOps::signedAngle(glm::vec2{1.0f, 0.0f}, glm::vec2{0.0f, 1.0f}) - Math::PI * 0.5f) < 0.0001f);
    assert(VectorOps::normalized4(glm::vec4{}) == glm::vec4{});
    assert(VectorOps::withAlpha(VectorOps::whiteColor(), 0.25f).w == 0.25f);
    assert(std::abs(VectorOps::linearToSrgb(VectorOps::srgbToLinear(glm::vec4(0.5f, 0.25f, 0.75f, 1.0f))).x - 0.5f) < 0.0001f);
    assert(Ease::inExpo(0.0f) == 0.0f && Ease::outExpo(1.0f) == 1.0f);
    assert(Ease::inOutBack(-1.0f) == 0.0f && Ease::inOutBack(2.0f) == 1.0f);
    assert(Ease::outBounce(0.0f) == 0.0f && Ease::outElastic(1.0f) == 1.0f);
    assert(Ease::outBounceUnclamped(1.2f) != Ease::outBounce(1.2f));
    assert(Bounds2Ops::contains(Bounds2{0.0f, 0.0f, 4.0f, 4.0f}, glm::vec2(2.0f, 0.0f)));
    assert(Bounds2Ops::intersects(Bounds2{0.0f, 0.0f, 4.0f, 4.0f}, Bounds2{1.0f, 0.0f, 4.0f, 4.0f}));
    assert(Noise::value2D(4u, 1.25f, 2.5f) == Noise::value2D(4u, 1.25f, 2.5f));
    assert(Noise::fbm2D(4u, 1.25f, 2.5f, 3) >= 0.0f && Noise::fbm2D(4u, 1.25f, 2.5f, 3) <= 1.0f);
    constexpr std::array<std::uint32_t, 20> random_golden{
        1013904223u, 1196435762u, 3519870697u, 2868466484u, 1649599747u,
        2670642822u, 1476291629u, 2748932008u, 2180890343u, 2498801434u,
        3421909937u, 3167820124u, 2636375307u, 3801544430u, 28987765u,
        2210837584u, 3039689583u, 1338634754u, 1649346937u, 2768872580u
    };
    std::uint32_t seed = 0u;
    for (const std::uint32_t expected : random_golden) {
        seed = Random::nextUInt(seed);
        assert(seed == expected);
    }
    assert(Random::rangeInt(1u, 9, -3) >= -3 && Random::rangeInt(1u, 9, -3) < 9);
    assert(Random::rangeInt(1u, 4, 4) == 4);
    assert(Random::rangeFloat(1u, 4.0f, -2.0f) >= -2.0f && Random::rangeFloat(1u, 4.0f, -2.0f) < 4.0f);
    assert(!Random::chance(1u, 0.0f) && Random::chance(1u, 1.0f));
    assert(VectorOps::length(Random::insideUnitCircle(8u)) <= 1.0f);
    assert(std::abs(VectorOps::length(Random::onUnitCircle(8u)) - 1.0f) < 0.0001f);
    const Rect inverted{4.0f, 4.0f, -4.0f, -4.0f};
    assert(RectOps::min(inverted) == glm::vec2(0.0f, 0.0f));
    assert(RectOps::max(inverted) == glm::vec2(4.0f, 4.0f));
    assert(RectOps::intersection(inverted, Rect{3.0f, 3.0f, 3.0f, 3.0f}).has_value());
    assert(!RectOps::intersection(inverted, Rect{4.0f, 0.0f, 1.0f, 1.0f}).has_value());
    std::vector<int> values{1, 2, 3, 4};
    EngineScript::shuffleValues(values, 7u);
    assert((values == std::vector<int>{2, 4, 1, 3}));
    assert(EngineScript::chooseValue(values, 7u).has_value());
    assert(!EngineScript::chooseValue(std::vector<int>{}, 7u).has_value());
    assert(catalog.has_available_type("Input"));
    assert(catalog.has_available_type("SpriteRenderer2D"));
    const ApiPropertyDescriptor* sprite_color = catalog.find_available_property("SpriteRenderer2D", "color");
    assert(sprite_color != nullptr);
    assert(sprite_color->type == "Color");
    assert(sprite_color->writable);
    assert(catalog.has_available_type("Audio"));
    assert(catalog.has_available_type("Scene"));
    assert(catalog.has_available_type("Particles"));
    assert(catalog.has_available_type("ParticleEffect2D"));
    assert(catalog.has_available_type("ParticleEmitOptions"));
    assert(catalog.find_available_type("ParticleEmitOptions")->value_type);
    assert(catalog.has_available_type("WorldText"));
    assert(catalog.has_available_type("FloatingTextOptions"));
    assert(catalog.find_available_type("FloatingTextOptions")->value_type);
    assert(catalog.has_available_function("delay"));
    assert(catalog.find_available_function("self") != nullptr);
    assert(catalog.find_available_function("self")->return_type == "Entity");
    NativeBehaviour owner;
    owner.entity_id = 42;
    owner.owner_entity = Entity{.id = 42, .name = "Owner"};
    assert(owner.self().id == 42);
    assert(owner.self().name == "Owner");
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
    const ApiMethodDescriptor* show_world_text = catalog.find_available_method("WorldText", "show");
    assert(show_world_text != nullptr);
    assert(show_world_text->return_type == "Bool");
    assert(show_world_text->parameters.size() == 3);
    const ApiMethodDescriptor* stop_emitter = catalog.find_available_method("ParticleEmitter2D", "stop");
    assert(stop_emitter != nullptr);
    assert(stop_emitter->min_arguments == 0);
    assert(stop_emitter->max_arguments == 1);
    assert(catalog.find_available_method("ParticleEmitter2D", "setEffect") != nullptr);
    assert(catalog.find_available_property("ParticleEmitter2D", "effectHandle") != nullptr);
    const ApiPropertyDescriptor* particle_scale = catalog.find_available_property("ParticleEmitOptions", "scale");
    assert(particle_scale != nullptr && particle_scale->writable);
  const ApiMethodDescriptor *find_gameplay_tag =
      catalog.find_available_method("Entities", "findByGameplayTag");
  assert(find_gameplay_tag != nullptr);
  assert(find_gameplay_tag->return_type == "Entity");
  assert(!find_gameplay_tag->nullable_return);
  assert(find_gameplay_tag->parameters.size() == 1);
  assert(find_gameplay_tag->parameters.at(0).type == "String");
  assert(catalog.find_available_method("Entities", "findAllByGameplayTag") !=
         nullptr);
  assert(catalog.find_available_method("Entities", "hasGameplayTag") !=
         nullptr);
  assert(catalog.find_available_method("Entities", "addGameplayTag") !=
         nullptr);
  assert(catalog.find_available_method("Entities", "removeGameplayTag") !=
         nullptr);
  const ApiMethodDescriptor *gameplay_tags =
      catalog.find_available_method("Entities", "gameplayTags");
  assert(gameplay_tags != nullptr);
  assert(gameplay_tags->return_type == "Array<String>");

  std::cout << "[PASS] EngineScriptApiCatalogTests passed!\n";
    return 0;
}
