#include "core/AssetDatabase.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "editor/commands/PlayModeCommands.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneManager.hpp"
#include "scene/SceneSerializer.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>

namespace {

class TestScene final : public Engine::Scene {
public:
    explicit TestScene(std::string name = "PlayModeIsolation") : Scene(std::move(name)) {}

    void set_is_editing(bool is_editing) override { m_is_editing = is_editing; }
    [[nodiscard]] bool is_editing() const override { return m_is_editing; }

    void on_update(float) override {
        if (m_is_editing) return;
        get_entity_manager().for_each<Engine::RigidBodyComponent>(
            [](Engine::EntityID, Engine::RigidBodyComponent& body) {
                body.velocity = {320.0f, -45.0f};
                body.acceleration = {4.0f, 8.0f};
                body.angular_velocity = 90.0f;
                body.mass = 42.0f;
                body.friction = 0.95f;
                body.active_forces.push_back({});
            }
        );
    }

private:
    bool m_is_editing{true};
};

void write_file(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << source;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace Engine;

    const fs::path root = fs::temp_directory_path() / "engine2d_play_mode_isolation_tests";
    std::error_code error;
    fs::remove_all(root, error);
    write_file(root / "assets/scripts/Move.luau", R"(--!strict
local script = { fields = {} }
function script.on_update(context, delta_time)
    local x, y, z = context.get_position()
    context.set_position(x + 20.0 * delta_time, y + 10.0 * delta_time, z)
end
return script
)");
    write_file(root / "assets/scripts/SwitchScene.luau", R"(--!strict
local script = { fields = {} }
function script.on_create(context)
    local scene = context.find_asset("assets/scenes/Runtime.scene")
    if scene == nil then error("runtime scene asset was not found") end
    local ok, reason = context.load_scene(scene)
    if not ok then error(reason or "runtime scene load failed") end
end
return script
)");
    write_file(root / "assets/scenes/Runtime.scene", "{}\n");
    AssetDatabase database(root);
    [[maybe_unused]] const AssetScanResult scan = database.scan(root / "assets");
    const AssetRecord* script = database.find_by_path("assets/scripts/Move.luau");
    const AssetRecord* switch_script = database.find_by_path("assets/scripts/SwitchScene.luau");
    assert(script);
    assert(switch_script);

    auto scene = std::make_shared<TestScene>();
    EntityManager& entities = scene->get_entity_manager();
    const EntityID entity = entities.create_entity();
    entities.add_component<TransformComponent>(entity, TransformComponent{2.0f, 3.0f, 0.0f});
    RigidBodyComponent authored_body = RigidBodyComponent::Dynamic();
    authored_body.mass = 2.5f;
    authored_body.friction = 0.25f;
    entities.add_component<RigidBodyComponent>(entity, authored_body);
    ScriptComponent component;
    component.script_handle = script->handle;
    entities.add_component<ScriptComponent>(entity, component);

    SceneManager manager;
    manager.set_asset_database(&database);
    manager.add_scene(scene);
    manager.start_play();
    assert(manager.is_playing());
    assert(manager.get_script_runtime().instance_count() == 1);
    manager.update(0.5f);
    assert(scene->get_entity_manager().get_component<TransformComponent>(entity).position.x == 12.0f);
    assert(scene->get_entity_manager().get_component<TransformComponent>(entity).position.y == 8.0f);
    const auto& runtime_body = scene->get_entity_manager().get_component<RigidBodyComponent>(entity);
    assert(runtime_body.velocity.x == 320.0f && runtime_body.velocity.y == -45.0f);
    assert(runtime_body.acceleration.x == 4.0f && runtime_body.acceleration.y == 8.0f);
    assert(runtime_body.angular_velocity == 90.0f);
    assert(runtime_body.mass == 42.0f && runtime_body.friction == 0.95f);
    assert(runtime_body.active_forces.size() == 1);
    manager.stop_play();
    assert(manager.is_editing());
    assert(!manager.get_script_runtime().is_running());
    const glm::vec3 restored = scene->get_entity_manager().get_component<TransformComponent>(entity).position;
    assert(restored.x == 2.0f && restored.y == 3.0f);
    assert(scene->get_entity_manager().get_component<ScriptComponent>(entity) == component);
    const auto& restored_body = scene->get_entity_manager().get_component<RigidBodyComponent>(entity);
    assert(restored_body.velocity == glm::vec2(0.0f));
    assert(restored_body.acceleration == glm::vec2(0.0f));
    assert(restored_body.angular_velocity == 0.0f);
    assert(restored_body.mass == authored_body.mass);
    assert(restored_body.friction == authored_body.friction);
    assert(restored_body.active_forces.empty());
    assert(manager.has_play_mode_changes());
    const auto x_change = std::find_if(
        manager.get_play_mode_changes().begin(),
        manager.get_play_mode_changes().end(),
        [entity](const PlayModePropertyChange& change) {
            return change.entity == entity && change.component == "transform" &&
                change.property == "position[0]";
        }
    );
    assert(x_change != manager.get_play_mode_changes().end());
    manager.select_play_mode_change(x_change->id, true);
    const std::optional<PlayModeApplySnapshot> apply_snapshot =
        manager.build_play_mode_apply_snapshot();
    assert(apply_snapshot && apply_snapshot->property_count == 1);
    SceneSerializer apply_serializer(scene.get());
    assert(apply_serializer.deserialize_from_string(apply_snapshot->after_json));
    const glm::vec3 applied = scene->get_entity_manager().get_component<TransformComponent>(entity).position;
    assert(applied.x == 12.0f && applied.y == 3.0f);
    assert(apply_serializer.deserialize_from_string(apply_snapshot->before_json));
    const glm::vec3 undone = scene->get_entity_manager().get_component<TransformComponent>(entity).position;
    assert(undone.x == 2.0f && undone.y == 3.0f);
    manager.clear_play_mode_changes();

    auto history_scene = std::make_shared<TestScene>("CommandHistoryIsolation");
    EntityManager& history_entities = history_scene->get_entity_manager();
    const EntityID history_entity = history_entities.create_entity();
    const TransformComponent initial_transform{1.0f, 2.0f, 0.0f};
    const TransformComponent edited_transform{5.0f, 2.0f, 0.0f};
    history_entities.add_component<TransformComponent>(history_entity, initial_transform);
    history_entities.add_component<ScriptComponent>(history_entity, component);

    EditorCommandHistory history;
    assert(history.execute_command(std::make_unique<SetComponentCommand<TransformComponent>>(
        &history_entities,
        history_entity,
        initial_transform,
        edited_transform,
        "Move Before Play"
    )));
    assert(history.get_undo_count() == 1 && history.get_current_revision() == 1);

    SceneManager history_manager;
    history_manager.set_asset_database(&database);
    history_manager.add_scene(history_scene);
    int invalidation_calls = 0;
    history_manager.set_on_authoring_state_invalidated([&]() { ++invalidation_calls; });
    history_manager.start_play();
    history_manager.update(1.0f);
    assert(history_entities.get_component<TransformComponent>(history_entity).position.x == 25.0f);
    history_manager.stop_play();
    assert(invalidation_calls == 0);
    assert(history_entities.get_component<TransformComponent>(history_entity).position.x == 5.0f);
    assert(history.get_undo_count() == 1 && history.get_current_revision() == 1);

    history.undo();
    assert(history_entities.get_component<TransformComponent>(history_entity).position.x == 1.0f);
    history.redo();
    assert(history_entities.get_component<TransformComponent>(history_entity).position.x == 5.0f);

    const auto history_x_change = std::find_if(
        history_manager.get_play_mode_changes().begin(),
        history_manager.get_play_mode_changes().end(),
        [history_entity](const PlayModePropertyChange& change) {
            return change.entity == history_entity && change.component == "transform" &&
                change.property == "position[0]";
        }
    );
    assert(history_x_change != history_manager.get_play_mode_changes().end());
    history_manager.select_play_mode_change(history_x_change->id, true);
    std::optional<PlayModeApplySnapshot> history_apply =
        history_manager.build_play_mode_apply_snapshot();
    assert(history_apply && history_apply->property_count == 1);
    assert(history.execute_command(std::make_unique<ApplyPlayModeChangesCommand>(
        history_scene.get(), nullptr, std::move(*history_apply)
    )));
    assert(history_entities.get_component<TransformComponent>(history_entity).position.x == 25.0f);
    assert(history.get_undo_count() == 2 && history.get_current_revision() == 2);
    history.undo();
    assert(history_entities.get_component<TransformComponent>(history_entity).position.x == 5.0f);
    history.undo();
    assert(history_entities.get_component<TransformComponent>(history_entity).position.x == 1.0f);
    history.redo();
    history.redo();
    assert(history_entities.get_component<TransformComponent>(history_entity).position.x == 25.0f);

    auto switching_scene = std::make_shared<TestScene>("SwitchingAuthoring");
    const EntityID switching_entity = switching_scene->get_entity_manager().create_entity();
    switching_scene->get_entity_manager().add_component<TransformComponent>(
        switching_entity, TransformComponent{}
    );
    ScriptComponent switching_component;
    switching_component.script_handle = switch_script->handle;
    switching_scene->get_entity_manager().add_component<ScriptComponent>(
        switching_entity, switching_component
    );

    SceneManager switching_manager;
    switching_manager.set_asset_database(&database);
    int load_calls = 0;
    switching_manager.set_runtime_scene_loader(
        [&](const std::string& path, std::string&) -> std::shared_ptr<Scene> {
            assert(path == "assets/scenes/Runtime.scene");
            ++load_calls;
            return std::make_shared<TestScene>("RuntimeLoaded");
        }
    );
    switching_manager.add_scene(switching_scene);
    switching_manager.start_play();
    assert(switching_manager.is_playing());
    assert(load_calls == 1);
    assert(switching_manager.get_active_scene() == switching_scene.get());
    switching_manager.update(0.0f);
    assert(switching_manager.get_active_scene() != switching_scene.get());
    assert(switching_manager.get_active_scene()->get_name() == "RuntimeLoaded");
    switching_manager.stop_play();
    assert(switching_manager.is_editing());
    assert(switching_manager.get_active_scene() == switching_scene.get());
    assert(switching_scene->get_entity_manager().has_component<ScriptComponent>(switching_entity));
    assert(!switching_manager.has_play_mode_changes());

    fs::remove_all(root, error);
    std::cout << "PlayModeIsolationTests passed\n";
    return 0;
}
