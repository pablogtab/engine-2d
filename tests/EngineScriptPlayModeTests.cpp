#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "core/AssetDatabase.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "scripting/ScriptRuntime.hpp"
#include "enginescript/editor/EngineScriptWorkspace.hpp"
#include "enginescript/async/AsyncScheduler.hpp"

using namespace Engine;
using namespace EngineScript;

#include <filesystem>
#include <fstream>

namespace {
void write_test_file(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << source;
}
} // namespace

void test_play_mode_lifecycle() {
    std::cout << "[Test] PlayModeLifecycle_StartsAndUpdates... ";
    
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_enginescript_playmode_tests";
    std::error_code error;
    fs::remove_all(root, error);

    std::string code = R"(
@behaviour
export class PlayerController extends Behaviour {
    @expose({ min: 0.0, max: 500.0 })
    speed: Float = 220.0;

    override update(deltaTime: Float): Void {}
}
)";
    write_test_file(root / "Assets/scripts/PlayerController.e2s", code);

    AssetDatabase asset_database(root);
    static_cast<void>(asset_database.scan("Assets"));

    const AssetRecord* record = asset_database.find_by_path(root / "Assets/scripts/PlayerController.e2s");
    assert(record != nullptr);
    assert(record->handle.is_valid());

    EntityManager entity_manager;
    ScriptRuntime runtime;

    EntityID entity = entity_manager.create_entity();
    ScriptComponent script_comp;
    script_comp.enabled = true;
    script_comp.script_handle = record->handle;
    entity_manager.add_component<ScriptComponent>(entity, script_comp);

    ScriptRuntimeServices services;
    auto start_res = runtime.start(entity_manager, asset_database, nullptr, services);
    assert(start_res.started == 1);
    assert(start_res.failed == 0);
    assert(runtime.instance_count() == 1);

    // Advance frames
    runtime.update(0.016f);
    runtime.update(0.016f);
    
    runtime.shutdown();
    assert(runtime.instance_count() == 0);
    fs::remove_all(root, error);
    std::cout << "PASSED\n";
}

void test_async_scheduler_cancellation() {
    std::cout << "[Test] AsyncScheduler_CancelsOnDestroy... ";
    
    AsyncScheduler scheduler;
    bool task_completed = false;
    uint32_t entity_scope = 42;

    scheduler.schedule_delay(0.1f, [&]() {
        task_completed = true;
    }, entity_scope);

    assert(scheduler.pending_tasks_count() == 1);

    // Cancel scope before delay expires
    scheduler.cancel_scope(entity_scope);
    scheduler.step(0.2f, 0.2f);

    assert(!task_completed);
    assert(scheduler.pending_tasks_count() == 0);

    std::cout << "PASSED\n";
}

void test_e2s_script_inspection() {
    std::cout << "[Test] ScriptInspection_InspectsE2SFields... ";
    
    EngineScriptWorkspace ws("");
    std::string script = R"(
@behaviour
export class Enemy extends Behaviour {
    @expose({ min: 1.0, max: 100.0 })
    health: Float = 100.0;

    @expose
    isAggressive: Bool = true;
}
)";
    ws.add_source_file("Enemy.e2s", script);
    auto res = ws.compile();
    assert(res.success);

    const auto& manifest = ws.metadata_manifest();
    assert(manifest.behaviours.size() == 1);
    assert(manifest.behaviours[0].name == "Enemy");
    assert(manifest.behaviours[0].properties.size() == 2);
    assert(manifest.behaviours[0].properties[0].name == "health");
    assert(manifest.behaviours[0].properties[1].name == "isAggressive");

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Running EngineScript Play Mode Tests ===\n";
    test_play_mode_lifecycle();
    test_async_scheduler_cancellation();
    test_e2s_script_inspection();
    std::cout << "=== All EngineScript Play Mode Tests Passed! ===\n";
    return 0;
}
