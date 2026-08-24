#include "core/AssetDatabase.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "scripting/ScriptRuntime.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void write_script(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << source;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace Engine;

    const fs::path root = fs::temp_directory_path() / "engine2d_script_hot_reload_tests";
    const fs::path script_path = root / "Assets/scripts/Reload.e2s";
    std::error_code error;
    fs::remove_all(root, error);
    write_script(script_path, R"(
@behaviour
export class ReloadBehaviour extends Behaviour {
    constructor(@component private body: RigidBody2D) {}

    @expose
    boost: Float = 1.0;

    @expose
    obsolete: Float = 5.0;

    override update(deltaTime: Float): Void {
        body.velocity.x = boost;
    }
}
)");

    AssetDatabase database(root);
    const AssetScanResult scan = database.scan("Assets");
    assert(scan.ready == 1);
    const AssetRecord* asset = database.find_by_path(root / "Assets/scripts/Reload.e2s");
    assert(asset && asset->type == AssetType::Script);

    EntityManager entities;
    const EntityID entity = entities.create_entity();
    entities.add_component<TransformComponent>(entity, TransformComponent{});
    entities.add_component<RigidBodyComponent>(entity, RigidBodyComponent::Dynamic());
    ScriptComponent component;
    component.script_handle = asset->handle;
    entities.add_component<ScriptComponent>(entity, component);

    ScriptRuntimeServices services;
    services.hot_reload_enabled = true;
    ScriptRuntime runtime;
    const ScriptRuntimeStartResult started = runtime.start(
        entities, database, nullptr, std::move(services)
    );
    assert(started.started == 1 && started.failed == 0);
    assert(runtime.instance_count() == 1);
    auto& live_script = entities.get_component<ScriptComponent>(entity);
    const auto boost = std::find_if(live_script.public_fields.begin(), live_script.public_fields.end(), [](const ScriptPublicField& field) {
        return field.name == "boost";
    });
    assert(boost != live_script.public_fields.end());
    boost->value = 7.0;
    runtime.update(0.016f);
    assert(std::abs(entities.get_component<RigidBodyComponent>(entity).velocity.x - 7.0f) < 0.001f);

    // Syntax failure hot reload
    write_script(script_path, "@behaviour\nexport class Broken { let x: =\n");
    const ScriptHotReloadResult syntax_failure = runtime.reload_changed_scripts(true);
    assert(syntax_failure.reloaded == 0 && syntax_failure.failed == 1);
    assert(runtime.instance_count() == 0);
    assert(runtime.status(entity) && runtime.status(entity)->state == ScriptExecutionState::Error);

    // Recovery hot reload
    write_script(script_path, R"(
@behaviour
export class ReloadBehaviour extends Behaviour {
    constructor(@component private body: RigidBody2D) {}

    @expose
    boost: Float = 10.0;

    override update(deltaTime: Float): Void {
        body.velocity.x = boost * 2.0;
    }
}
)");
    const ScriptHotReloadResult recovered = runtime.reload_changed_scripts(true);
    assert(recovered.reloaded == 1 && recovered.failed == 0);
    assert(!runtime.reload_error(asset->handle));
    runtime.update(0.016f);
    assert(std::abs(entities.get_component<RigidBodyComponent>(entity).velocity.x - 14.0f) < 0.001f);

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "ScriptHotReloadTests passed\n";
    return 0;
}
