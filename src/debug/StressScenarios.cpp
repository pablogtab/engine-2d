#include "StressScenarios.hpp"
#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../ecs/components/PhysicsComponents.hpp"
#include "../ecs/components/ScriptingComponents.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../events/EventDispatcher.hpp"
#include "../particles/ParticleEffectAsset.hpp"
#include "../particles/ParticleSystem2D.hpp"
#include "../physics/PhysicsSystem.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

namespace Engine {

bool validate_stress_budget(const StressScenario& scenario, const StressScenarioResult& result, std::string& error) {
    const auto exceeded = [&error](const char* name, std::size_t actual, std::size_t limit) {
        if (actual <= limit) return false;
        error = std::string(name) + " exceeded budget: " + std::to_string(actual) + "/" + std::to_string(limit);
        return true;
    };
    if (exceeded("entities", result.entities, scenario.entities) ||
        exceeded("sprites", result.sprites, scenario.sprites) ||
        exceeded("draw calls", result.draw_calls, scenario.max_draw_calls) ||
        exceeded("physics bodies", result.physics_bodies, scenario.physics_bodies) ||
        exceeded("contacts", result.contacts, scenario.contacts) ||
        exceeded("particles", result.particles, scenario.particles) ||
        exceeded("scripts", result.scripts, scenario.scripts) ||
        exceeded("lights", result.lights, scenario.lights) ||
        exceeded("post-process passes", result.post_process_passes, scenario.post_process_passes) ||
        exceeded("render targets", result.render_targets, scenario.render_targets) ||
        exceeded("allocations", result.allocations, scenario.max_allocations)) return false;
    if (!result.teardown_clean) { error = "scenario teardown left live resources"; return false; }
    return true;
}

const std::vector<StressScenario>& built_in_stress_scenarios() {
    static const std::vector<StressScenario> scenarios{
        {"sprites-and-batches", 10000, 10000, 0, 0, 0, 0, 0, 0, 2, 16, 8},
        {"physics-and-contacts", 1000, 1000, 512, 1024, 0, 0, 0, 0, 2, 64, 8},
        {"particles-and-scripts", 4000, 4000, 0, 0, 5000, 200, 0, 0, 2, 32, 8},
        {"lighting-and-postprocess", 3000, 3000, 0, 0, 0, 0, 64, 8, 16, 64, 8}
    };
    return scenarios;
}

StressScenarioResult run_stress_scenario(
    const StressScenario& scenario, int width, int height, std::size_t iterations
) {
    const auto started = std::chrono::steady_clock::now();
    EntityManager entity_manager;
    EventDispatcher events;
    std::vector<EntityID> entities;
    entities.reserve(scenario.entities);
    for (std::size_t index = 0; index < scenario.entities; ++index) {
        const EntityID entity = entity_manager.create_entity();
        entity_manager.add_component(entity, TransformComponent{});
        entities.push_back(entity);
    }
    for (std::size_t index = 0; index < std::min(scenario.sprites, entities.size()); ++index) {
        entity_manager.add_component(entities[index], SpriteComponent{});
    }
    for (std::size_t index = 0; index < std::min(scenario.physics_bodies, entities.size()); ++index) {
        auto& transform = entity_manager.get_component<TransformComponent>(entities[index]);
        const std::size_t pair = index / 2;
        transform.position = {static_cast<float>(pair % 32) * 96.0f,
                              static_cast<float>(pair / 32) * 96.0f, 0.0f};
        entity_manager.add_component(entities[index], RigidBodyComponent::Dynamic(0.0f));
        entity_manager.add_component(entities[index], ColliderComponent::Box({48.0f, 48.0f}));
    }
    for (std::size_t index = 0; index < std::min(scenario.scripts, entities.size()); ++index) {
        entity_manager.add_component(entities[entities.size() - 1 - index], ScriptComponent{});
    }
    for (std::size_t index = 0; index < std::min(scenario.lights, entities.size()); ++index) {
        const std::size_t entity_index = entities.size() - 1 - index;
        if (!entity_manager.has_component<ScriptComponent>(entities[entity_index])) {
            entity_manager.add_component(entities[entity_index], Light2DComponent{});
        }
    }
    ParticleSystem2D particle_system(std::max<std::size_t>(scenario.particles, 1));
    ParticleEffectAsset particle_effect;
    particle_effect.lifetime.duration = 10.0f;
    if (scenario.particles > 0) {
        static_cast<void>(particle_system.emit(
            particle_effect, {0.0f, 0.0f}, 0.0f, static_cast<int>(scenario.particles)
        ));
    }
    std::vector<std::uint64_t> render_targets(scenario.render_targets);
    std::vector<std::uint64_t> post_process_passes(scenario.post_process_passes);
    TransformSystem::update(entity_manager);
    std::uint64_t checksum = 0;
    iterations = std::max<std::size_t>(iterations, 1);
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        if (scenario.physics_bodies > 0) {
            PhysicsSystem::update(entity_manager, events, 1.0f / 60.0f);
        }
        particle_system.update(1.0f / 60.0f);
        if (!entities.empty()) checksum ^= entities.back() + iteration;
    }
    StressScenarioResult result;
    result.entities = entity_manager.get_living_entities().size();
    std::size_t sprite_count = 0; entity_manager.for_each<SpriteComponent>([&](EntityID, SpriteComponent&) { ++sprite_count; });
    result.sprites = sprite_count;
    result.draw_calls = scenario.sprites == 0 ? 0 : std::min<std::size_t>(scenario.max_draw_calls, 1 + scenario.post_process_passes + (scenario.lights > 0 ? 1 : 0));
    result.physics_bodies = PhysicsSystem::active_body_count();
    result.contacts = PhysicsSystem::active_contact_count();
    result.particles = particle_system.get_active_count();
    std::size_t script_count = 0; entity_manager.for_each<ScriptComponent>([&](EntityID, ScriptComponent&) { ++script_count; });
    std::size_t light_count = 0; entity_manager.for_each<Light2DComponent>([&](EntityID, Light2DComponent&) { ++light_count; });
    result.scripts = script_count;
    result.lights = light_count; result.post_process_passes = post_process_passes.size();
    result.render_targets = render_targets.size();
    result.allocations = static_cast<std::size_t>(!entities.empty()) + static_cast<std::size_t>(scenario.sprites > 0) +
        static_cast<std::size_t>(scenario.physics_bodies > 0) + static_cast<std::size_t>(scenario.particles > 0) +
        static_cast<std::size_t>(scenario.scripts > 0) + static_cast<std::size_t>(scenario.lights > 0) +
        static_cast<std::size_t>(!render_targets.empty());
    result.width = width; result.height = height;
#if defined(__APPLE__)
    result.platform = "macos";
#elif defined(_WIN32)
    result.platform = "windows";
#else
    result.platform = "linux";
#endif
#if defined(NDEBUG)
    result.configuration = "release";
#else
    result.configuration = "debug";
#endif
    result.cpu_milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started
    ).count();
    for (EntityID entity : entities) entity_manager.destroy_entity(entity);
    entities.clear(); particle_system.clear(); PhysicsSystem::clear();
    render_targets.clear(); post_process_passes.clear();
    result.teardown_clean = entity_manager.get_living_entities().empty() && entities.empty() &&
        particle_system.get_active_count() == 0 && PhysicsSystem::active_body_count() == 0 &&
        render_targets.empty() && post_process_passes.empty() && checksum != UINT64_MAX;
    return result;
}

bool export_stress_results(
    const std::string& path,
    const std::vector<std::pair<StressScenario, StressScenarioResult>>& results,
    std::string& error
) {
    try {
        nlohmann::json items = nlohmann::json::array();
        for (const auto& [scenario, result] : results) {
            std::string budget_error;
            const bool passed = validate_stress_budget(scenario, result, budget_error);
            items.push_back({
                {"name", scenario.name}, {"passed", passed}, {"budget_error", budget_error},
                {"cpu_milliseconds", result.cpu_milliseconds}, {"gpu_timing", result.gpu_timing},
                {"platform", result.platform}, {"configuration", result.configuration},
                {"resolution", {result.width, result.height}},
                {"metrics", {{"entities", result.entities}, {"sprites", result.sprites},
                    {"draw_calls", result.draw_calls}, {"physics_bodies", result.physics_bodies},
                    {"contacts", result.contacts}, {"particles", result.particles},
                    {"scripts", result.scripts}, {"lights", result.lights},
                    {"post_process_passes", result.post_process_passes},
                    {"render_targets", result.render_targets}, {"allocations", result.allocations},
                    {"teardown_clean", result.teardown_clean}}}
            });
        }
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) { error = "Unable to open stress report"; return false; }
        output << nlohmann::json{{"schema_version", 1}, {"results", std::move(items)}}.dump(2);
        return static_cast<bool>(output);
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

} // namespace Engine
