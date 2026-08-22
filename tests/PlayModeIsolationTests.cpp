#include "core/AssetDatabase.hpp"
#include "ecs/components/CoreComponents.hpp"
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
    manager.stop_play();
    assert(manager.is_editing());
    assert(!manager.get_script_runtime().is_running());
    const glm::vec3 restored = scene->get_entity_manager().get_component<TransformComponent>(entity).position;
    assert(restored.x == 2.0f && restored.y == 3.0f);
    assert(scene->get_entity_manager().get_component<ScriptComponent>(entity) == component);
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
