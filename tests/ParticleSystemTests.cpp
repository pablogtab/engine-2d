#include "core/AssetDatabase.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/ParticleComponents.hpp"
#include "particles/ParticleEffectAsset.hpp"
#include "particles/ParticleSystem2D.hpp"
#include "particles/ParticleWorld.hpp"
#include "scene/BuiltinComponentRegistrations.hpp"
#include "scene/ComponentRegistry.hpp"
#include "scene/Scene.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << content;
}

} // namespace

int main() {
    using namespace Engine;
    namespace fs = std::filesystem;

    std::cout << "[RUNNING] ParticleSystemTests..." << std::endl;

    // -------------------------------------------------------------
    // Test 1: ParticleEffectAsset Serialization & Deserialization
    // -------------------------------------------------------------
    {
        ParticleEffectAsset authored;
        authored.name = "DustBurst";
        authored.texture_handle = AssetHandle{12345};
        authored.appearance.start_color = {255, 200, 100, 255};
        authored.appearance.end_color = {100, 50, 0, 0};
        authored.appearance.start_size = 12.0f;
        authored.appearance.end_size = 2.0f;
        authored.appearance.size_variation = 3.0f;
        authored.motion.initial_velocity = {50.0f, -120.0f};
        authored.motion.velocity_variation = {10.0f, 20.0f};
        authored.motion.acceleration = {0.0f, 50.0f};
        authored.motion.gravity = {0.0f, 98.0f};
        authored.motion.initial_rotation = 45.0f;
        authored.motion.rotation_variation = 15.0f;
        authored.motion.rotation_speed = 90.0f;
        authored.motion.rotation_speed_variation = 30.0f;
        authored.lifetime.duration = 1.5f;
        authored.lifetime.variation = 0.3f;
        authored.collision.enabled = true;
        authored.collision.mask_bits = 0x0003;
        authored.collision.bounciness = 0.6f;
        authored.emission.continuous_rate = 25.0f;
        authored.emission.burst_count = 15;
        authored.emission.duration = 2.0f;
        authored.emission.start_delay = 0.1f;
        authored.emission.loop = true;
        authored.rendering.render_layer = 1;
        authored.rendering.z_layer = 0.5f;
        authored.rendering.blend_mode = ParticleBlendMode::Additive;

        nlohmann::json json = authored.to_json();
        assert(json["name"] == "DustBurst");
        assert(json["version"] == 1);
        assert(json["appearance"]["start_color"][0] == 255);
        assert(json["appearance"]["start_size"] == 12.0f);
        assert(json["collision"]["enabled"] == true);
        assert(json["emission"]["burst_count"] == 15);

        auto parsed = ParticleEffectAsset::from_json(json);
        assert(parsed.has_value());
        assert(*parsed == authored);
        assert(parsed->name == "DustBurst");
        assert(parsed->texture_handle.value == 12345);
        assert(parsed->appearance.start_color.g == 200);
        assert(parsed->appearance.end_size == 2.0f);
        assert(parsed->motion.initial_velocity.y == -120.0f);
        assert(parsed->lifetime.duration == 1.5f);
        assert(parsed->collision.bounciness == 0.6f);
        assert(parsed->collision.mask_bits == 0x0003);
        assert(parsed->emission.continuous_rate == 25.0f);

        std::cout << "  - Test 1 passed: ParticleEffectAsset serialization & deserialization" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 2: AssetDatabase Integration and On-Disk Roundtrip
    // -------------------------------------------------------------
    {
        const fs::path root = fs::temp_directory_path() / "engine2d_particle_asset_tests";
        std::error_code ec;
        fs::remove_all(root, ec);

        AssetDatabase db(root);
        const fs::path particle_path = root / "Assets/Particles/Explosion.particle";

        ParticleEffectAsset asset;
        asset.name = "Explosion";
        asset.appearance.start_size = 32.0f;
        asset.emission.burst_count = 50;

        std::string err;
        bool saved = asset.save_asset(particle_path, db, &err);
        assert(saved);
        assert(err.empty());
        assert(fs::exists(particle_path));

        const AssetRecord* record = db.find_by_path(particle_path);
        assert(record != nullptr);
        assert(record->type == AssetType::ParticleEffect);
        assert(record->status == AssetStatus::Ready);

        auto loaded = ParticleEffectAsset::load_asset(record->handle, db);
        assert(loaded.has_value());
        assert(loaded->name == "Explosion");
        assert(loaded->appearance.start_size == 32.0f);
        assert(loaded->emission.burst_count == 50);

        fs::remove_all(root, ec);
        std::cout << "  - Test 2 passed: AssetDatabase integration and atomic on-disk roundtrip" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 3: ParticleSystem2D Determinism and Simulation
    // -------------------------------------------------------------
    {
        ParticleSystem2D system1(100);
        ParticleSystem2D system2(100);

        system1.set_seed(42);
        system2.set_seed(42);

        ParticleEffectAsset asset;
        asset.appearance.start_size = 10.0f;
        asset.appearance.end_size = 0.0f;
        asset.motion.initial_velocity = {100.0f, 0.0f};
        asset.motion.velocity_variation = {20.0f, 10.0f};
        asset.motion.initial_rotation = 0.0f;
        asset.motion.rotation_variation = 30.0f;
        asset.lifetime.duration = 1.0f;

        assert(system1.emit(asset, {0.0f, 0.0f}, 0.0f, 10) == 10);
        assert(system2.emit(asset, {0.0f, 0.0f}, 0.0f, 10) == 10);

        assert(system1.get_active_count() == 10);
        assert(system2.get_active_count() == 10);

        // Update 0.5s
        system1.update(0.5f);
        system2.update(0.5f);

        assert(system1.get_active_count() == 10);
        assert(system2.get_active_count() == 10);

        // Update another 0.6s -> particles expire (lifetime 1.0s)
        system1.update(0.6f);
        assert(system1.get_active_count() == 0);

        system1.clear();
        assert(system1.get_active_count() == 0);

        std::cout << "  - Test 3 passed: ParticleSystem2D simulation and lifecycle" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 4: ParticleWorld Scene Integration & Play Mode Isolation
    // -------------------------------------------------------------
    {
        const fs::path root = fs::temp_directory_path() / "engine2d_particle_world_tests";
        std::error_code ec;
        fs::remove_all(root, ec);

        AssetDatabase db(root);
        const fs::path particle_path = root / "Assets/smoke.particle";
        ParticleEffectAsset smoke_asset;
        smoke_asset.name = "Smoke";
        smoke_asset.emission.continuous_rate = 20.0f;
        smoke_asset.lifetime.duration = 0.5f;
        static_cast<void>(smoke_asset.save_asset(particle_path, db));

        const AssetRecord* record = db.find_by_path(particle_path);
        assert(record);

        Scene scene("TestParticleScene");
        auto& em = scene.get_entity_manager();
        EntityID emitter_ent = em.create_entity();
        em.add_component<TransformComponent>(emitter_ent, TransformComponent{10.0f, 20.0f, 0.0f});

        ParticleEmitter2DComponent emitter;
        emitter.effect_handle = record->handle;
        emitter.play_on_awake = true;
        emitter.active = true;
        em.add_component<ParticleEmitter2DComponent>(emitter_ent, emitter);

        ParticleWorld world;
        assert(world.system().get_active_count() == 0);

        // Start emitting
        assert(world.play_entity(emitter_ent, scene, db));
        assert(em.get_component<ParticleEmitter2DComponent>(emitter_ent).is_playing);

        // Update 0.1s -> should have emitted particles (rate 20/s * 0.1s = 2)
        world.update(scene, 0.1f, db);
        assert(world.system().get_active_count() >= 2);

        // Burst
        assert(world.burst_entity(emitter_ent, scene, db, 5));
        assert(world.system().get_active_count() >= 7);

        // Play Mode Stop / Reset isolation
        world.clear();
        assert(world.system().get_active_count() == 0);

        fs::remove_all(root, ec);
        std::cout << "  - Test 4 passed: ParticleWorld scene updates and Play Mode isolation" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 5: ComponentRegistry Registration & Serialization
    // -------------------------------------------------------------
    {
        ComponentRegistry& registry = default_component_registry();
        const auto* desc = registry.find_by_key("particle_emitter_2d");
        assert(desc != nullptr);
        assert(desc->schema_version == 1);

        ParticleEmitter2DComponent emitter;
        emitter.effect_handle = AssetHandle{999};
        emitter.active = true;
        emitter.play_on_awake = false;
        emitter.local_space = true;
        emitter.emit_rate_override = 45.0f;
        emitter.burst_count_override = 12;

        EntityManager em;
        EntityID ent = em.create_entity();
        em.add_component<ParticleEmitter2DComponent>(ent, emitter);

        ComponentSerializationContext ctx;
        nlohmann::json json = desc->serialize(em, ent, ctx);
        assert(json["active"] == true);
        assert(json["play_on_awake"] == false);
        assert(json["local_space"] == true);
        assert(json["emit_rate_override"] == 45.0f);
        assert(json["burst_count_override"] == 12);

        EntityID target_ent = em.create_entity();
        std::string error;
        bool ok = desc->deserialize(em, target_ent, json, ctx, error);
        assert(ok);
        assert(error.empty());
        assert(em.has_component<ParticleEmitter2DComponent>(target_ent));
        const auto& deserialized = em.get_component<ParticleEmitter2DComponent>(target_ent);
        assert(deserialized.effect_handle.value == 999);
        assert(deserialized.active == true);
        assert(deserialized.play_on_awake == false);
        assert(deserialized.local_space == true);
        assert(deserialized.emit_rate_override == 45.0f);
        assert(deserialized.burst_count_override == 12);

        std::cout << "  - Test 5 passed: ComponentRegistry serialization of ParticleEmitter2DComponent" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 6: Versioned asset validation rejects partial and invalid contracts
    // -------------------------------------------------------------
    {
        ParticleEffectAsset valid;
        nlohmann::json json = valid.to_json();
        std::string error;

        nlohmann::json missing_section = json;
        missing_section.erase("motion");
        assert(!ParticleEffectAsset::from_json(missing_section, &error));
        assert(error.find("motion") != std::string::npos);

        nlohmann::json wrong_version = json;
        wrong_version["version"] = 99;
        assert(!ParticleEffectAsset::from_json(wrong_version, &error));

        nlohmann::json invalid_number = json;
        invalid_number["lifetime"]["duration"] = -1.0f;
        assert(!ParticleEffectAsset::from_json(invalid_number, &error));

        nlohmann::json invalid_color = json;
        invalid_color["appearance"]["start_color"][0] = 300;
        assert(!ParticleEffectAsset::from_json(invalid_color, &error));

        nlohmann::json unsupported_blend = json;
        unsupported_blend["rendering"]["blend_mode"] = "Multiply";
        assert(!ParticleEffectAsset::from_json(unsupported_blend, &error));

        std::cout << "  - Test 6 passed: strict particle asset schema validation" << std::endl;
    }

    // -------------------------------------------------------------
    // Test 7: Pool and per-emitter limits drop safely without overwriting
    // -------------------------------------------------------------
    {
        ParticleSystem2D system(4, 7);
        ParticleEffectAsset asset;
        asset.lifetime.duration = 10.0f;

        assert(system.emit(asset, {}, 0.0f, 4, nullptr, 10, false, 3) == 3);
        assert(system.get_emitter_active_count(10) == 3);
        assert(system.get_dropped_count() == 1);
        assert(system.emit(asset, {}, 0.0f, 2, nullptr, 20, false, 3) == 1);
        assert(system.get_active_count() == 4);
        assert(system.get_dropped_count() == 2);

        system.clear_emitter(10);
        assert(system.get_emitter_active_count(10) == 0);
        assert(system.get_active_count() == 1);

        std::cout << "  - Test 7 passed: bounded emission and source ownership" << std::endl;
    }

    std::cout << "[ALL TESTS PASSED] ParticleSystemTests successful!" << std::endl;
    return 0;
}
