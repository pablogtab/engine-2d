#include "core/AssetDatabase.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "scripting/ScriptRuntime.hpp"

#include <cassert>
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
    @expose
    boost: Float = 1.0;

    @expose
    obsolete: Float = 5.0;

    override update(deltaTime: Float): Void {}
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

    // Syntax failure hot reload
    write_script(script_path, "@behaviour\nexport class Broken { let x: =\n");
    const ScriptHotReloadResult syntax_failure = runtime.reload_changed_scripts(true);
    assert(syntax_failure.reloaded == 0 && syntax_failure.failed == 1);
    assert(!runtime.status(entity) || runtime.status(entity)->state == ScriptExecutionState::Running);

    // Recovery hot reload
    write_script(script_path, R"(
@behaviour
export class ReloadBehaviour extends Behaviour {
    @expose
    boost: Float = 10.0;

    override update(deltaTime: Float): Void {}
}
)");
    const ScriptHotReloadResult recovered = runtime.reload_changed_scripts(true);
    assert(recovered.reloaded == 1 && recovered.failed == 0);
    assert(!runtime.reload_error(asset->handle));

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "ScriptHotReloadTests passed\n";
    return 0;
}
