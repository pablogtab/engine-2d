#include "core/AssetDatabase.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "scene/CustomScene.hpp"
#include "scene/SceneManager.hpp"
#include "scene/SceneSerializer.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {

class TestScene final : public Engine::CustomScene {
public:
    explicit TestScene(std::string name = "PlayModeIsolation") : CustomScene(std::move(name)) {}

    void set_is_editing(bool is_editing) override { m_is_editing = is_editing; }
    [[nodiscard]] bool is_editing() const override { return m_is_editing; }

    void on_update(float dt) override {
        if (m_is_editing) return;
        get_entity_manager().for_each<Engine::TransformComponent>(
            [dt](Engine::EntityID, Engine::TransformComponent& transform) {
                transform.position.x += 20.0f * dt;
                transform.position.y += 10.0f * dt;
            }
        );
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
    write_file(root / "Assets/scripts/Move.e2s", R"(
@behaviour
export class Move extends Behaviour {
    override update(deltaTime: Float): Void {}
}
)");
    write_file(root / "Assets/scripts/SwitchScene.e2s", R"(
@behaviour
export class SwitchScene extends Behaviour {
    constructor(@inject private scene: Scene) {}

    override on_start(): Void {
        scene.load("Assets/scenes/Runtime.scene");
    }
}
)");
    write_file(root / "Assets/scenes/Runtime.scene", "{}\n");
    AssetDatabase database(root);
    [[maybe_unused]] const AssetScanResult scan = database.scan("Assets");
    const AssetRecord* script = database.find_by_path(root / "Assets/scripts/Move.e2s");
    const AssetRecord* switch_script = database.find_by_path(root / "Assets/scripts/SwitchScene.e2s");
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
    manager.update(0.0f);
    manager.update(0.0f);
    manager.update(0.0f);
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

    auto switching_scene = std::make_shared<TestScene>("DeferredSceneSource");
    const EntityID switching_entity = switching_scene->get_entity_manager().create_entity();
    ScriptComponent switching_component;
    switching_component.script_handle = switch_script->handle;
    switching_scene->get_entity_manager().add_component<ScriptComponent>(switching_entity, switching_component);
    SceneManager switching_manager;
    switching_manager.set_asset_database(&database);
    switching_manager.set_runtime_scene_loader([](const std::string& path, std::string& error) -> std::shared_ptr<Scene> {
        if (path != "Assets/scenes/Runtime.scene") {
            error = "unexpected scene path";
            return std::shared_ptr<Scene>{};
        }
        return std::make_shared<TestScene>("DeferredRuntimeScene");
    });
    switching_manager.add_scene(switching_scene);
    switching_manager.start_play();
    switching_manager.update(0.0f);
    switching_manager.update(0.0f);
    switching_manager.update(0.0f);
    assert(switching_manager.game_session().state() == GameSession::TransitionState::Requested);
    assert(switching_manager.is_runtime_scene_loading());
    switching_manager.update(0.0f);
    assert(switching_manager.game_session().state() == GameSession::TransitionState::Requested);
    switching_manager.update(0.0f);
    assert(switching_manager.game_session().state() == GameSession::TransitionState::Loaded);
    assert(!switching_manager.is_runtime_scene_loading());
    assert(switching_manager.get_active_scene()->get_name() == "DeferredRuntimeScene");
    switching_manager.stop_play();

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
    history_manager.update(0.0f);
    history_manager.update(0.0f);
    history_manager.update(0.0f);
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

    fs::remove_all(root, error);
    std::cout << "PlayModeIsolationTests passed\n";
    return 0;
}
