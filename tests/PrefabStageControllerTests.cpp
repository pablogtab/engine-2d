// PrefabStageControllerTests.cpp
// Tests the SceneManager transient scene infrastructure and PrefabStageController
// open/save/discard/close lifecycle.

#include "scene/SceneManager.hpp"
#include "scene/Scene.hpp"
#include "scene/CustomScene.hpp"
#include "scene/EntitySubtreeArchive.hpp"
#include "editor/stage/PrefabStageController.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/IEditorCommand.hpp"
#include "editor/EditorSelection.hpp"
#include "editor/state/EditorEntityStateStore.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/TransformSystem.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "renderer/Renderer2D.hpp"
#include "core/FileSystem.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace Engine;

class ScopedPrefabTestWorkspace {
public:
    ScopedPrefabTestWorkspace() {
        std::error_code error;
        m_previous_path = std::filesystem::current_path(error);
        assert(!error);

        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_workspace_path = std::filesystem::temp_directory_path(error) /
            ("engine2d_prefab_stage_" + std::to_string(timestamp));
        assert(!error);

        std::filesystem::create_directories(m_workspace_path / "assets/prefabs", error);
        assert(!error);
        std::filesystem::current_path(m_workspace_path, error);
        assert(!error);
    }

    ~ScopedPrefabTestWorkspace() {
        std::error_code error;
        std::filesystem::current_path(m_previous_path, error);
        std::filesystem::remove_all(m_workspace_path, error);
    }

private:
    std::filesystem::path m_previous_path;
    std::filesystem::path m_workspace_path;
};


// ──────────────────────────────────────────────────────────────────────────────
// Spy scene for verifying update / render / resize dispatch
// ──────────────────────────────────────────────────────────────────────────────
class SpyScene : public CustomScene {
public:
    SpyScene(std::string name) : CustomScene(std::move(name)) {}
    int update_count{0};
    int render_count{0};
    int resize_count{0};
    float last_delta{0.0f};

    void on_update(float dt) override {
        update_count++;
        last_delta = dt;
    }
    void on_render(Renderer2D&) override {
        render_count++;
    }
    void on_viewport_resize(RenderViewKind, const RenderViewConfiguration&) override {
        resize_count++;
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Minimal no-op command — solely to dirty the command history in tests
// ──────────────────────────────────────────────────────────────────────────────
struct NopCommand : IEditorCommand {
    bool execute() override { return true; }
    bool undo() override { return true; }
    std::string get_description() const override { return "Nop"; }
};

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

static SceneManager make_scene_manager_with_authoring_scene() {
    SceneManager sm;
    auto scene = std::make_shared<Scene>("AuthoringScene");
    sm.add_scene(scene);
    return sm;
}

/// Saves a minimal two-entity prefab to disk and returns the path.
static std::string write_test_prefab(const std::string& filename = "test_stage.prefab") {
    EntityManager em;
    EntityID root = em.create_entity();
    em.add_component<TagComponent>(root, TagComponent{"StagePrefab"});
    em.add_component<TransformComponent>(root, TransformComponent{glm::vec3(0.0f)});

    EntityID child = em.create_entity();
    em.add_component<TagComponent>(child, TagComponent{"Child"});
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(5.0f, 0.0f, 0.0f)});
    static_cast<void>(em.set_parent(child, root));

    TransformSystem::update(em);

    std::filesystem::create_directories("assets/prefabs");
    const std::string path = "assets/prefabs/" + filename;
    ComponentSerializationContext ctx;
    std::string json = EntitySubtreeArchive::capture_to_json(em, root, ctx);
    assert(!json.empty());
    FileSystem::write_text(path, json);
    return path;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 1: Transient scene is NOT in m_scenes
// ──────────────────────────────────────────────────────────────────────────────
void test_transient_scene_not_in_scene_list() {
    SceneManager sm = make_scene_manager_with_authoring_scene();
    assert(sm.get_scene_count() == 1);

    auto stage = std::make_shared<Scene>("Prefab: TestPrefab");
    sm.enter_transient_scene(stage);

    assert(sm.is_transient_scene_active());
    assert(sm.get_scene_count() == 1); // NOT added to m_scenes
    assert(sm.get_active_scene() == stage.get()); // but IS the active scene

    sm.exit_transient_scene();
    assert(!sm.is_transient_scene_active());
    assert(sm.get_scene_count() == 1);
    assert(sm.get_active_scene() != stage.get()); // original authoring scene restored

    std::cout << "test_transient_scene_not_in_scene_list passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 2: next_scene / previous_scene are blocked during transient stage
// ──────────────────────────────────────────────────────────────────────────────
void test_transient_scene_blocks_navigation() {
    SceneManager sm;
    sm.add_scene(std::make_shared<Scene>("Scene1"));
    sm.add_scene(std::make_shared<Scene>("Scene2"));
    sm.set_active_scene(size_t(0));
    assert(sm.get_active_index() == 0);

    auto stage = std::make_shared<Scene>("Prefab: Foo");
    sm.enter_transient_scene(stage);

    sm.next_scene(); // no-op
    assert(sm.is_transient_scene_active());
    assert(sm.get_active_scene() == stage.get());

    sm.exit_transient_scene();
    assert(sm.get_active_index() == 0); // index unchanged

    std::cout << "test_transient_scene_blocks_navigation passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 3: open() creates isolated session and selects root entity
// ──────────────────────────────────────────────────────────────────────────────
void test_prefab_stage_open_isolated_session() {
    const std::string prefab_path = write_test_prefab("iso_session_test.prefab");
    SceneManager sm = make_scene_manager_with_authoring_scene();

    PrefabStageController ctrl;
    bool ok = ctrl.open(prefab_path, &sm, nullptr);
    assert(ok);
    assert(ctrl.is_active());
    assert(ctrl.get_prefab_name() == "iso_session_test");
    assert(ctrl.get_root_entity() != 0);
    assert(!ctrl.is_dirty()); // clean on open

    assert(ctrl.get_command_history() != nullptr);
    assert(ctrl.get_entity_state_store() != nullptr);
    assert(ctrl.get_selection() != nullptr);

    assert(sm.is_transient_scene_active());
    assert(sm.get_active_scene() == ctrl.get_stage_scene());
    assert(ctrl.get_selection()->is_selected(ctrl.get_root_entity()));

    ctrl.close_discard();
    assert(!ctrl.is_active());
    assert(!sm.is_transient_scene_active());

    std::filesystem::remove(prefab_path);
    std::cout << "test_prefab_stage_open_isolated_session passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 4: save() atomically writes to disk and marks history clean
// ──────────────────────────────────────────────────────────────────────────────
void test_prefab_stage_edit_and_atomic_save() {
    const std::string prefab_path = write_test_prefab("atomic_save_test.prefab");
    SceneManager sm = make_scene_manager_with_authoring_scene();

    PrefabStageController ctrl;
    assert(ctrl.open(prefab_path, &sm, nullptr));

    // Dirty the stage history
    ctrl.get_command_history()->push_executed_command(std::make_unique<NopCommand>());
    assert(ctrl.is_dirty());

    bool saved = ctrl.save();
    assert(saved);
    assert(!ctrl.is_dirty());
    assert(!std::filesystem::exists(prefab_path + ".tmp"));
    assert(std::filesystem::exists(prefab_path));

    std::string content = FileSystem::read_text(prefab_path);
    assert(!content.empty());
    assert(content.find("prefab_version") != std::string::npos);

    ctrl.close_discard();
    std::filesystem::remove(prefab_path);
    std::cout << "test_prefab_stage_edit_and_atomic_save passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 5: close_discard() does NOT touch the disk asset
// ──────────────────────────────────────────────────────────────────────────────
void test_prefab_stage_discard_preserves_disk_asset() {
    const std::string prefab_path = write_test_prefab("discard_test.prefab");
    const std::string original = FileSystem::read_text(prefab_path);
    SceneManager sm = make_scene_manager_with_authoring_scene();

    PrefabStageController ctrl;
    assert(ctrl.open(prefab_path, &sm, nullptr));

    ctrl.get_command_history()->push_executed_command(std::make_unique<NopCommand>());
    assert(ctrl.is_dirty());

    ctrl.close_discard();
    assert(!ctrl.is_active());

    assert(FileSystem::read_text(prefab_path) == original);

    std::filesystem::remove(prefab_path);
    std::cout << "test_prefab_stage_discard_preserves_disk_asset passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 6: close_discard() restores the authoring scene as active
// ──────────────────────────────────────────────────────────────────────────────
void test_prefab_stage_close_restores_authoring_scene() {
    const std::string prefab_path = write_test_prefab("restore_test.prefab");
    auto authoring = std::make_shared<Scene>("AuthoringScene");
    SceneManager sm;
    sm.add_scene(authoring);

    PrefabStageController ctrl;
    assert(ctrl.open(prefab_path, &sm, nullptr));

    Scene* stage_ptr = ctrl.get_stage_scene();
    assert(sm.get_active_scene() == stage_ptr);

    ctrl.close_discard();

    assert(!sm.is_transient_scene_active());
    assert(sm.get_active_scene() == authoring.get());
    assert(sm.get_active_scene() != stage_ptr);

    std::filesystem::remove(prefab_path);
    std::cout << "test_prefab_stage_close_restores_authoring_scene passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 7: Opening a missing / corrupt file fails cleanly
// ──────────────────────────────────────────────────────────────────────────────
void test_prefab_stage_corrupt_file_fails_cleanly() {
    SceneManager sm = make_scene_manager_with_authoring_scene();
    Scene* authoring_before = sm.get_active_scene();

    // Case A: file doesn't exist
    PrefabStageController ctrl;
    bool ok = ctrl.open("assets/prefabs/nonexistent_xyz.prefab", &sm, nullptr);
    assert(!ok);
    assert(!ctrl.is_active());
    assert(!sm.is_transient_scene_active());
    assert(sm.get_active_scene() == authoring_before);

    // Case B: corrupt JSON
    const std::string corrupt_path = "assets/prefabs/corrupt_stage_test.prefab";
    std::filesystem::create_directories("assets/prefabs");
    FileSystem::write_text(corrupt_path, "{ not valid prefab json !!! }");

    bool ok2 = ctrl.open(corrupt_path, &sm, nullptr);
    assert(!ok2);
    assert(!ctrl.is_active());
    assert(!sm.is_transient_scene_active());
    assert(sm.get_active_scene() == authoring_before);

    std::filesystem::remove(corrupt_path);
    std::cout << "test_prefab_stage_corrupt_file_fails_cleanly passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 8: Cannot open a second stage while one is already active
// ──────────────────────────────────────────────────────────────────────────────
void test_prefab_stage_blocks_double_open() {
    const std::string path_a = write_test_prefab("stage_a.prefab");
    const std::string path_b = write_test_prefab("stage_b.prefab");
    SceneManager sm = make_scene_manager_with_authoring_scene();

    PrefabStageController ctrl;
    assert(ctrl.open(path_a, &sm, nullptr));
    assert(ctrl.is_active());

    bool ok2 = ctrl.open(path_b, &sm, nullptr);
    assert(!ok2); // blocked
    assert(ctrl.get_prefab_name() == "stage_a"); // state unchanged

    ctrl.close_discard();
    std::filesystem::remove(path_a);
    std::filesystem::remove(path_b);
    std::cout << "test_prefab_stage_blocks_double_open passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 9: Transient scene receives update, render, and viewport resize calls
// ──────────────────────────────────────────────────────────────────────────────
void test_transient_scene_receives_update_and_render() {
    SceneManager sm;
    auto authoring = std::make_shared<SpyScene>("Authoring");
    sm.add_scene(authoring);
    sm.set_active_scene(size_t(0));

    Renderer2D renderer(nullptr);

    // Initially authoring scene receives updates and renders
    sm.update(0.016f);
    assert(authoring->update_count == 1);
    sm.render(renderer);
    assert(authoring->render_count == 1);

    auto stage = std::make_shared<SpyScene>("Prefab: Stage");
    sm.enter_transient_scene(stage);
    assert(sm.is_transient_scene_active());

    // When transient scene is active, it must receive update, render, and resize
    sm.update(0.033f);
    assert(stage->update_count == 1);
    assert(stage->last_delta == 0.033f);
    assert(authoring->update_count == 1); // authoring scene should NOT receive updates

    sm.render(renderer);
    assert(stage->render_count == 1);
    assert(authoring->render_count == 1); // authoring scene should NOT receive renders

    // Test configure_view dispatch
    RenderViewConfiguration config;
    config.output_width = 800;
    config.output_height = 600;
    sm.configure_view(RenderViewKind::Editor, config);
    assert(stage->resize_count >= 1);

    sm.exit_transient_scene();
    assert(!sm.is_transient_scene_active());

    // After exit, authoring scene resumes receiving updates and renders
    sm.update(0.016f);
    assert(authoring->update_count == 2);
    assert(stage->update_count == 1);

    sm.render(renderer);
    assert(authoring->render_count == 2);
    assert(stage->render_count == 1);

    std::cout << "test_transient_scene_receives_update_and_render passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 10: Root entity protection query helpers
// ──────────────────────────────────────────────────────────────────────────────
void test_prefab_stage_root_protection_helpers() {
    const std::string prefab_path = write_test_prefab("protect_test.prefab");
    SceneManager sm = make_scene_manager_with_authoring_scene();

    PrefabStageController ctrl;
    assert(ctrl.open(prefab_path, &sm, nullptr));

    EntityID root = ctrl.get_root_entity();
    assert(root != 0);

    // Root entity must be detected and protected
    assert(ctrl.is_root_entity(root));
    assert(!ctrl.can_delete_entity(root));
    assert(!ctrl.can_reparent_entity(root));

    // Other IDs are not root and can be manipulated
    EntityID other = root + 100;
    assert(!ctrl.is_root_entity(other));
    assert(ctrl.can_delete_entity(other));
    assert(ctrl.can_reparent_entity(other));

    ctrl.close_discard();

    // When stage is closed, is_root_entity is false for all
    assert(!ctrl.is_root_entity(root));
    assert(ctrl.can_delete_entity(root));

    std::filesystem::remove(prefab_path);
    std::cout << "test_prefab_stage_root_protection_helpers passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Test 11: FileSystem::write_text_atomically replaces existing file cleanly
// ──────────────────────────────────────────────────────────────────────────────
void test_file_system_write_text_atomically() {
    const std::string test_file = "assets/prefabs/atomic_fs_test.json";
    std::filesystem::create_directories("assets/prefabs");

    // Write initial
    assert(FileSystem::write_text_atomically(test_file, "{\"v\": 1}"));
    assert(FileSystem::exists(test_file));
    assert(FileSystem::read_text(test_file) == "{\"v\": 1}");
    assert(!FileSystem::exists(test_file + ".tmp"));

    // Overwrite atomically
    assert(FileSystem::write_text_atomically(test_file, "{\"v\": 2, \"updated\": true}"));
    assert(FileSystem::exists(test_file));
    assert(FileSystem::read_text(test_file) == "{\"v\": 2, \"updated\": true}");
    assert(!FileSystem::exists(test_file + ".tmp"));

    std::filesystem::remove(test_file);
    std::cout << "test_file_system_write_text_atomically passed." << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Main
// ──────────────────────────────────────────────────────────────────────────────
int main() {
    ScopedPrefabTestWorkspace workspace;

    test_transient_scene_not_in_scene_list();
    test_transient_scene_blocks_navigation();
    test_prefab_stage_open_isolated_session();
    test_prefab_stage_edit_and_atomic_save();
    test_prefab_stage_discard_preserves_disk_asset();
    test_prefab_stage_close_restores_authoring_scene();
    test_prefab_stage_corrupt_file_fails_cleanly();
    test_prefab_stage_blocks_double_open();
    test_transient_scene_receives_update_and_render();
    test_prefab_stage_root_protection_helpers();
    test_file_system_write_text_atomically();

    std::cout << "\n=== All PrefabStageControllerTests passed! ===" << std::endl;
    return 0;
}
