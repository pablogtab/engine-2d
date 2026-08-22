#include "core/AssetDatabase.hpp"
#include "core/FileSystem.hpp"
#include "core/Input.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/components/AnimationComponents.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "editor/scripting/ScriptAnalyzer.hpp"
#include "editor/scripting/ScriptTemplates.hpp"
#include "scripting/ScriptRuntime.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void write_file(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << source;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace Engine;

    const fs::path root = fs::temp_directory_path() / "engine2d_luau_runtime_tests";
    std::error_code error;
    fs::remove_all(root, error);
    write_file(root / "assets/scripts/Movement.luau", R"(--!strict
local script = {
    fields = {
        { name = "speed", type = "number", default = 4.0 },
        { name = "active", type = "boolean", default = true },
        { name = "label", type = "string", default = "Mover" },
        { name = "offset", type = "vector2", default = {1.0, 2.0} },
        { name = "target", type = "entity", default = 0 },
        { name = "asset", type = "asset", default = nil },
    },
}
function script.on_create(context)
    local x, y, z = context.get_position()
    context.set_position(x, y + 1.0, z)
end
function script.on_update(context, delta_time)
    local x, y, z = context.get_position()
    context.set_position(x + context.fields.speed * delta_time, y, z)
end
function script.on_destroy(context)
    local x, _, z = context.get_position()
    context.set_position(x, 99.0, z)
end
return script
)");
    write_file(root / "assets/scripts/NoStrict.luau", "return {}\n");
    const std::string platformer_source = FileSystem::read_text(
        std::string(ENGINE_SOURCE_DIR) + "/assets/scripts/PlatformerPlayer.luau"
    );
    assert(!platformer_source.empty());
    write_file(root / "assets/scripts/PlatformerPlayer.luau", platformer_source);

    AssetDatabase database(root);
    const AssetScanResult scan = database.scan(root / "assets");
    assert(scan.ready == 3);
    const AssetRecord* movement = database.find_by_path("assets/scripts/Movement.luau");
    const AssetRecord* no_strict = database.find_by_path("assets/scripts/NoStrict.luau");
    const AssetRecord* platformer = database.find_by_path("assets/scripts/PlatformerPlayer.luau");
    assert(movement && movement->type == AssetType::Script);
    assert(no_strict && no_strict->type == AssetType::Script);
    assert(platformer && platformer->type == AssetType::Script);

    const ScriptInspectionResult inspection = ScriptRuntime::inspect_asset(movement->handle, database);
    assert(inspection.success);
    assert(inspection.public_fields.size() == 6);
    assert(!ScriptRuntime::inspect_asset(no_strict->handle, database).success);
    const ScriptInspectionResult platformer_inspection =
        ScriptRuntime::inspect_asset(platformer->handle, database);
    assert(platformer_inspection.success);
    assert(ScriptAnalyzer::analyze_source(platformer_source, "PlatformerPlayer").success);

    EntityManager entities;
    const EntityID entity = entities.create_entity();
    entities.add_component<TransformComponent>(entity, TransformComponent{});
    ScriptComponent component;
    component.script_handle = movement->handle;
    component.public_fields = inspection.public_fields;
    std::get<double>(component.public_fields.front().value) = 10.0;
    entities.add_component<ScriptComponent>(entity, component);

    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(entities, database);
    assert(started.started == 1 && started.failed == 0);
    assert(runtime.instance_count() == 1);
    assert(entities.get_component<TransformComponent>(entity).position.y == 1.0f);
    runtime.update(0.5f);
    assert(entities.get_component<TransformComponent>(entity).position.x == 5.0f);
    assert(runtime.status(entity) && runtime.status(entity)->state == ScriptExecutionState::Running);
    runtime.shutdown();
    assert(entities.get_component<TransformComponent>(entity).position.y == 99.0f);

    Input::map_axis("Horizontal", AxisBinding{
        SDL_SCANCODE_A,
        SDL_SCANCODE_D,
        GamepadAxis::LEFT_X,
        false,
        {SDL_SCANCODE_LEFT},
        {SDL_SCANCODE_RIGHT}
    });
    SDL_Event key_down{};
    key_down.type = SDL_EVENT_KEY_DOWN;
    key_down.key.scancode = SDL_SCANCODE_RIGHT;
    Input::process_event(key_down);

    const EntityID platformer_entity = entities.create_entity();
    entities.add_component<TransformComponent>(platformer_entity, TransformComponent{});
    entities.add_component<RigidBodyComponent>(platformer_entity, RigidBodyComponent{});
    entities.add_component<SpriteComponent>(platformer_entity, SpriteComponent{});
    AnimationComponent animation;
    animation.add_clip("Idle", 0, 1);
    animation.add_clip("Run", 1, 1);
    animation.add_clip("Jump", 2, 1);
    animation.add_clip("Fall", 3, 1);
    entities.add_component<AnimationComponent>(platformer_entity, animation);
    ScriptComponent platformer_component;
    platformer_component.script_handle = platformer->handle;
    platformer_component.public_fields = platformer_inspection.public_fields;
    entities.add_component<ScriptComponent>(platformer_entity, platformer_component);

    const ScriptRuntimeStartResult platformer_started = runtime.start(entities, database);
    assert(platformer_started.started == 2 && platformer_started.failed == 0);
    runtime.update(1.0f / 60.0f);
    assert(entities.get_component<RigidBodyComponent>(platformer_entity).velocity.x == 280.0f);
    assert(!entities.get_component<SpriteComponent>(platformer_entity).flip_x);
    assert(runtime.status(platformer_entity));
    assert(runtime.status(platformer_entity)->state == ScriptExecutionState::Running);
    auto& live_fields = entities.get_component<ScriptComponent>(platformer_entity).public_fields;
    const auto walk_speed = std::find_if(
        live_fields.begin(), live_fields.end(),
        [](const ScriptPublicField& field) { return field.name == "walk_speed"; }
    );
    assert(walk_speed != live_fields.end());
    walk_speed->value = 840.0;
    runtime.update(1.0f / 60.0f);
    assert(entities.get_component<RigidBodyComponent>(platformer_entity).velocity.x == 840.0f);
    runtime.shutdown();
    Input::reset_states();

    const EntityID invalid_entity = entities.create_entity();
    entities.add_component<TransformComponent>(invalid_entity, TransformComponent{});
    ScriptComponent invalid_component;
    invalid_component.script_handle = no_strict->handle;
    entities.add_component<ScriptComponent>(invalid_entity, invalid_component);
    const ScriptRuntimeStartResult restarted = runtime.start(entities, database);
    assert(restarted.started == 2 && restarted.failed == 1);
    assert(runtime.status(invalid_entity));
    assert(runtime.status(invalid_entity)->state == ScriptExecutionState::Error);
    runtime.shutdown();

    const ScriptAnalysisResult valid = ScriptAnalyzer::analyze_source(
        "--!strict\nlocal value: number = 1\nreturn { value = value }\n"
    );
    assert(valid.success);
    assert(ScriptAnalyzer::analyze_source(std::string(DEFAULT_SCRIPT_TEMPLATE)).success);
    const ScriptAnalysisResult invalid = ScriptAnalyzer::analyze_source(
        "--!strict\nlocal value: number = 'wrong'\nreturn { value = value }\n"
    );
    assert(!invalid.success && !invalid.diagnostics.empty());
    assert(!ScriptAnalyzer::analyze_source("return {}\n").success);

    fs::remove_all(root, error);
    std::cout << "LuauRuntimeTests passed\n";
    return 0;
}
