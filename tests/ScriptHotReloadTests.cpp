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
    const fs::path script_path = root / "assets/scripts/Reload.luau";
    std::error_code error;
    fs::remove_all(root, error);
    write_script(script_path, R"(--!strict
local counter = 0
local script = {
    fields = {
        { name = "boost", type = "number", default = 1.0 },
        { name = "obsolete", type = "number", default = 5.0 },
    },
}
function script.on_update(context, _delta_time)
    counter += 1
    local x, y, z = context.get_position()
    context.set_position(x + counter, y, z)
end
return script
)");

    AssetDatabase database(root);
    const AssetScanResult scan = database.scan(root / "assets");
    assert(scan.ready == 1);
    const AssetRecord* asset = database.find_by_path("assets/scripts/Reload.luau");
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
    runtime.update(0.0f);
    runtime.update(0.0f);
    assert(entities.get_component<TransformComponent>(entity).position.x == 3.0f);

    write_script(script_path, "--!strict\nlocal broken: number =\n");
    const ScriptHotReloadResult syntax_failure = runtime.reload_changed_scripts(true);
    assert(syntax_failure.reloaded == 0 && syntax_failure.failed == 1);
    assert(runtime.reload_error(asset->handle));
    assert(runtime.status(entity)->state == ScriptExecutionState::Running);
    runtime.update(0.0f);
    assert(entities.get_component<TransformComponent>(entity).position.x == 6.0f);

    write_script(script_path, R"(--!strict
local script = { fields = {} }
function script.on_create(_context)
    error("initialization failed")
end
return script
)");
    const ScriptHotReloadResult runtime_failure = runtime.reload_changed_scripts(true);
    assert(runtime_failure.reloaded == 0 && runtime_failure.failed == 1);
    assert(runtime.status(entity)->state == ScriptExecutionState::Running);
    runtime.update(0.0f);
    assert(entities.get_component<TransformComponent>(entity).position.x == 10.0f);

    write_script(script_path, R"(--!strict
local counter = 0
local script = {
    fields = {
        { name = "boost", type = "number", default = 10.0 },
    },
}
function script.on_create(context)
    if context.fields.obsolete ~= nil then error("removed field survived reload") end
    local x, _, z = context.get_position()
    context.set_position(x, 5.0, z)
end
function script.on_update(context, _delta_time)
    counter += 1
    local x, y, z = context.get_position()
    context.set_position(x + counter * context.fields.boost, y, z)
end
return script
)");
    const ScriptHotReloadResult recovered = runtime.reload_changed_scripts(true);
    assert(recovered.reloaded == 1 && recovered.failed == 0);
    assert(!runtime.reload_error(asset->handle));
    assert(entities.get_component<TransformComponent>(entity).position.y == 5.0f);
    runtime.update(0.0f);
    assert(entities.get_component<TransformComponent>(entity).position.x == 20.0f);
    runtime.update(0.0f);
    assert(entities.get_component<TransformComponent>(entity).position.x == 40.0f);

    runtime.shutdown();
    fs::remove_all(root, error);
    std::cout << "ScriptHotReloadTests passed\n";
    return 0;
}
