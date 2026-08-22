#include "core/AssetDatabase.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneManager.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {

class TestScene final : public Engine::Scene {
public:
    TestScene() : Scene("PlayModeIsolation") {}
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
    context.set_position(x + 20.0 * delta_time, y, z)
end
return script
)");
    AssetDatabase database(root);
    [[maybe_unused]] const AssetScanResult scan = database.scan(root / "assets");
    const AssetRecord* script = database.find_by_path("assets/scripts/Move.luau");
    assert(script);

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
    manager.stop_play();
    assert(manager.is_editing());
    assert(!manager.get_script_runtime().is_running());
    const glm::vec3 restored = scene->get_entity_manager().get_component<TransformComponent>(entity).position;
    assert(restored.x == 2.0f && restored.y == 3.0f);
    assert(scene->get_entity_manager().get_component<ScriptComponent>(entity) == component);

    fs::remove_all(root, error);
    std::cout << "PlayModeIsolationTests passed\n";
    return 0;
}
