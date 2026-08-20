#include "scene/EntitySubtreeArchive.hpp"
#include "scene/PrefabSerializer.hpp"
#include "scene/PrefabPropagation.hpp"
#include "scene/PrefabOverrides.hpp"
#include "editor/commands/PrefabCommands.hpp"

#include "editor/commands/EntityCommands.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/EditorSelection.hpp"
#include "editor/state/EditorEntityStateStore.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/TransformSystem.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "tilemap/Tilemap.hpp"
#include "core/FileSystem.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

using namespace Engine;

static bool approx_equal(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) < eps;
}

static bool approx_vec3(const glm::vec3& a, const glm::vec3& b, float eps = 0.001f) {
    return approx_equal(a.x, b.x, eps) && approx_equal(a.y, b.y, eps) && approx_equal(a.z, b.z, eps);
}

static bool approx_vec2(const glm::vec2& a, const glm::vec2& b, float eps = 0.001f) {
    return approx_equal(a.x, b.x, eps) && approx_equal(a.y, b.y, eps);
}

void test_prefab_serialization_and_deserialization_round_trip() {
    EntityManager em;
    EntityID root = em.create_entity();
    em.add_component<TagComponent>(root, TagComponent{"Enemy_Prefab"});
    em.add_component<TransformComponent>(root, TransformComponent{glm::vec3(50.0f, 50.0f, 0.0f), glm::vec2(1.5f, 1.5f), 15.0f});

    EntityID child1 = em.create_entity();
    em.add_component<TagComponent>(child1, TagComponent{"Sword"});
    em.add_component<TransformComponent>(child1, TransformComponent{glm::vec3(10.0f, 5.0f, 0.0f)});
    static_cast<void>(em.set_parent(child1, root));

    EntityID child2 = em.create_entity();
    em.add_component<TagComponent>(child2, TagComponent{"Shield"});
    em.add_component<TransformComponent>(child2, TransformComponent{glm::vec3(-10.0f, 0.0f, 0.0f)});
    em.add_component<RigidBodyComponent>(child2, RigidBodyComponent{});
    static_cast<void>(em.set_parent(child2, root));

    TransformSystem::update(em);

    ComponentSerializationContext ctx;
    std::string json_text = EntitySubtreeArchive::capture_to_json(em, root, ctx);
    assert(!json_text.empty());

    // Instantiate into fresh EntityManager at world position (100, 200, 0)
    EntityManager em2;
    TransformComponent desired_world{glm::vec3(100.0f, 200.0f, 0.0f)};
    auto result = EntitySubtreeArchive::instantiate_from_json(em2, json_text, desired_world, 0, ctx);

    assert(result.success);
    assert(result.created_entities.size() == 3);
    EntityID new_root = result.root_entity;
    assert(em2.is_alive(new_root));
    assert(em2.has_component<TagComponent>(new_root));
    assert(em2.get_component<TagComponent>(new_root).tag == "Enemy_Prefab");

    const auto& new_root_tr = em2.get_component<TransformComponent>(new_root);
    assert(approx_vec3(new_root_tr.world_position, glm::vec3(100.0f, 200.0f, 0.0f)));

    // Verify children are parented to new_root
    assert(em2.has_component<HierarchyComponent>(new_root));
    const auto& children = em2.get_component<HierarchyComponent>(new_root).children;
    assert(children.size() == 2);

    std::cout << "test_prefab_serialization_and_deserialization_round_trip passed." << std::endl;
}

void test_prefab_subtree_preserves_geometry_from_rotated_scaled_parent() {
    EntityManager em;
    EntityID external_parent = em.create_entity();
    em.add_component<TransformComponent>(external_parent, TransformComponent{glm::vec3(500.0f, 500.0f, 0.0f), glm::vec2(2.0f, 2.0f), 45.0f});

    EntityID prefab_root = em.create_entity();
    em.add_component<TagComponent>(prefab_root, TagComponent{"Turret"});
    em.add_component<TransformComponent>(prefab_root, TransformComponent{glm::vec3(20.0f, 0.0f, 0.0f), glm::vec2(1.5f, 1.5f), 10.0f});
    static_cast<void>(em.set_parent(prefab_root, external_parent));

    EntityID barrel = em.create_entity();
    em.add_component<TagComponent>(barrel, TagComponent{"Barrel"});
    em.add_component<TransformComponent>(barrel, TransformComponent{glm::vec3(5.0f, 0.0f, 0.0f)});
    static_cast<void>(em.set_parent(barrel, prefab_root));

    TransformSystem::update(em);

    float orig_world_rot = em.get_component<TransformComponent>(prefab_root).world_rotation;
    glm::vec2 orig_world_scl = em.get_component<TransformComponent>(prefab_root).world_scale;

    ComponentSerializationContext ctx;
    std::string json_text = EntitySubtreeArchive::capture_to_json(em, prefab_root, ctx);
    assert(!json_text.empty());

    // Instantiate as root entity in a new scene
    EntityManager em2;
    auto result = EntitySubtreeArchive::instantiate_from_json(em2, json_text, std::nullopt, 0, ctx);
    assert(result.success);

    EntityID instantiated_root = result.root_entity;
    const auto& tr2 = em2.get_component<TransformComponent>(instantiated_root);
    assert(approx_equal(tr2.world_rotation, orig_world_rot));
    assert(approx_vec2(tr2.world_scale, orig_world_scl));

    std::cout << "test_prefab_subtree_preserves_geometry_from_rotated_scaled_parent passed." << std::endl;
}

void test_prefab_internal_entity_reference_remapping() {
    EntityManager em;
    EntityID dummy = em.create_entity(); // ID 1
    (void)dummy;

    EntityID root = em.create_entity(); // ID 2
    em.add_component<TagComponent>(root, TagComponent{"Vehicle"});
    em.add_component<TransformComponent>(root, TransformComponent{});

    EntityID driver = em.create_entity(); // ID 3
    em.add_component<TagComponent>(driver, TagComponent{"Driver"});
    em.add_component<TransformComponent>(driver, TransformComponent{});
    static_cast<void>(em.set_parent(driver, root));

    // Camera on root follows child driver
    CameraComponent cam;
    cam.follow_target = driver;
    em.add_component<CameraComponent>(root, cam);

    TransformSystem::update(em);

    ComponentSerializationContext ctx;
    std::string json_text = EntitySubtreeArchive::capture_to_json(em, root, ctx);

    EntityManager em2;
    auto result = EntitySubtreeArchive::instantiate_from_json(em2, json_text, std::nullopt, 0, ctx);
    assert(result.success);

    EntityID new_root = result.root_entity;
    assert(em2.has_component<CameraComponent>(new_root));
    const auto& new_cam = em2.get_component<CameraComponent>(new_root);

    // Camera follow target must NOT be driver (old ID), but the new driver ID
    assert(new_cam.follow_target != driver);
    assert(new_cam.follow_target != 0);
    assert(em2.is_alive(new_cam.follow_target));
    assert(em2.has_component<TagComponent>(new_cam.follow_target));
    assert(em2.get_component<TagComponent>(new_cam.follow_target).tag == "Driver");

    std::cout << "test_prefab_internal_entity_reference_remapping passed." << std::endl;
}

void test_prefab_external_entity_reference_cleared() {
    EntityManager em;
    EntityID external_hero = em.create_entity();
    em.add_component<TagComponent>(external_hero, TagComponent{"External_Hero"});
    em.add_component<TransformComponent>(external_hero, TransformComponent{});

    EntityID root = em.create_entity();
    em.add_component<TagComponent>(root, TagComponent{"Follower"});
    em.add_component<TransformComponent>(root, TransformComponent{});

    // Camera follows external_hero (outside prefab subtree)
    CameraComponent cam;
    cam.follow_target = external_hero;
    em.add_component<CameraComponent>(root, cam);

    TransformSystem::update(em);

    ComponentSerializationContext ctx;
    std::string json_text = EntitySubtreeArchive::capture_to_json(em, root, ctx);

    EntityManager em2;
    auto result = EntitySubtreeArchive::instantiate_from_json(em2, json_text, std::nullopt, 0, ctx);
    assert(result.success);

    EntityID new_root = result.root_entity;
    assert(em2.has_component<CameraComponent>(new_root));
    const auto& new_cam = em2.get_component<CameraComponent>(new_root);
    // External reference must be safely cleared to 0
    assert(new_cam.follow_target == 0);

    std::cout << "test_prefab_external_entity_reference_cleared passed." << std::endl;
}

void test_prefab_instantiation_under_transformed_parent() {
    EntityManager em;
    EntityID dest_parent = em.create_entity();
    em.add_component<TagComponent>(dest_parent, TagComponent{"Platform"});
    em.add_component<TransformComponent>(dest_parent, TransformComponent{glm::vec3(200.0f, 100.0f, 0.0f), glm::vec2(2.0f, 2.0f), 0.0f});

    EntityManager source_em;
    EntityID root = source_em.create_entity();
    source_em.add_component<TagComponent>(root, TagComponent{"Item"});
    source_em.add_component<TransformComponent>(root, TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});

    TransformSystem::update(source_em);
    TransformSystem::update(em);

    ComponentSerializationContext ctx;
    std::string json_text = EntitySubtreeArchive::capture_to_json(source_em, root, ctx);

    // Instantiate under dest_parent with desired world transform (300, 100, 0)
    TransformComponent desired_world{glm::vec3(300.0f, 100.0f, 0.0f)};
    auto result = EntitySubtreeArchive::instantiate_from_json(em, json_text, desired_world, dest_parent, ctx);
    assert(result.success);

    EntityID new_root = result.root_entity;
    assert(em.has_component<HierarchyComponent>(new_root));
    assert(em.get_component<HierarchyComponent>(new_root).parent == dest_parent);

    const auto& tr = em.get_component<TransformComponent>(new_root);
    assert(approx_vec3(tr.world_position, glm::vec3(300.0f, 100.0f, 0.0f)));

    std::cout << "test_prefab_instantiation_under_transformed_parent passed." << std::endl;
}

void test_prefab_atomic_rollback_on_corrupt_data() {
    EntityManager em;
    EntityID existing = em.create_entity();
    em.add_component<TagComponent>(existing, TagComponent{"PreExisting"});
    em.add_component<TransformComponent>(existing, TransformComponent{});

    ComponentSerializationContext ctx;
    std::string corrupt_json = "{\"prefab_version\": 1, \"entities\": [{\"id\": 1, \"invalid_syntax\": }";
    auto result = EntitySubtreeArchive::instantiate_from_json(em, corrupt_json, std::nullopt, 0, ctx);
    assert(!result.success);
    assert(!result.error_message.empty());

    // Verify existing entity is untouched and no leak occurred
    assert(em.is_alive(existing));
    size_t count = 0;
    em.for_each<TransformComponent>([&](EntityID id, const TransformComponent&) {
        count++;
        assert(id == existing);
    });
    assert(count == 1);

    std::cout << "test_prefab_atomic_rollback_on_corrupt_data passed." << std::endl;
}

void test_instantiate_prefab_command_undo_redo_without_disk_dependency() {
    EntityManager em;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;

    // Create a temporary prefab file
    std::string temp_prefab = "assets/prefabs/temp_test_item.prefab";
    EntityManager src_em;
    EntityID root = src_em.create_entity();
    src_em.add_component<TagComponent>(root, TagComponent{"TempItem"});
    src_em.add_component<TransformComponent>(root, TransformComponent{glm::vec3(10.0f, 20.0f, 0.0f)});
    TransformSystem::update(src_em);

    assert(PrefabSerializer::save_to_file(src_em, root, temp_prefab));

    // Execute InstantiatePrefabCommand
    auto cmd = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, temp_prefab, false, TransformComponent{glm::vec3(400.0f, 300.0f, 0.0f)}
    );

    history.execute_command(std::move(cmd));
    assert(history.can_undo());
    assert(selection->count() == 1);
    EntityID created_id = *selection->get_selected().begin();
    assert(em.is_alive(created_id));
    assert(approx_vec3(em.get_component<TransformComponent>(created_id).world_position, glm::vec3(400.0f, 300.0f, 0.0f)));

    // Delete the prefab file from disk
    std::filesystem::remove(temp_prefab);
    assert(!std::filesystem::exists(temp_prefab));

    // Undo: entity must be destroyed
    history.undo();
    assert(!em.is_alive(created_id));
    assert(selection->empty());

    // Redo: entity must be recreated cleanly from in-memory snapshot without touching disk
    history.redo();
    assert(em.is_alive(created_id));
    assert(selection->is_selected(created_id));
    assert(approx_vec3(em.get_component<TransformComponent>(created_id).world_position, glm::vec3(400.0f, 300.0f, 0.0f)));

    std::cout << "test_instantiate_prefab_command_undo_redo_without_disk_dependency passed." << std::endl;
}

void test_prefab_physics_and_tilemap_undo_redo() {
    EntityManager em;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;

    // Create source entity with physics components (RigidBody + Collider)
    EntityManager src_em;
    EntityID root = src_em.create_entity();
    src_em.add_component<TagComponent>(root, TagComponent{"PhysicsBox"});
    src_em.add_component<TransformComponent>(root, TransformComponent{glm::vec3(50.0f, 60.0f, 0.0f)});
    RigidBodyComponent rb;
    rb.body_type = BodyType::Dynamic;
    rb.mass = 2.5f;
    src_em.add_component<RigidBodyComponent>(root, rb);

    ColliderComponent col;
    ColliderShape shape;
    shape.type = ColliderShapeType::Box;
    shape.size = {32.0f, 32.0f};
    col.shapes.push_back(shape);
    src_em.add_component<ColliderComponent>(root, col);

    // Create child with a populated TilemapComponent
    EntityID tilemap_child = src_em.create_entity();
    src_em.add_component<TagComponent>(tilemap_child, TagComponent{"Ground_Tilemap"});
    src_em.add_component<TransformComponent>(tilemap_child, TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});
    TilemapComponent tilemap_comp;
    tilemap_comp.tile_size = 48.0f;
    tilemap_comp.friction = 0.65f;
    tilemap_comp.set_tile(0, 0, 1);
    tilemap_comp.set_tile(1, 0, 2);
    tilemap_comp.set_tile(2, 0, 3);
    src_em.add_component<TilemapComponent>(tilemap_child, tilemap_comp);
    static_cast<void>(src_em.set_parent(tilemap_child, root));

    TransformSystem::update(src_em);

    std::string json_text = EntitySubtreeArchive::capture_to_json(src_em, root, ComponentSerializationContext{});
    assert(!json_text.empty());

    auto cmd = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, json_text, true, TransformComponent{glm::vec3(100.0f, 100.0f, 0.0f)}
    );

    history.execute_command(std::move(cmd));
    assert(selection->count() == 1);
    EntityID created_root = *selection->get_selected().begin();
    assert(em.is_alive(created_root));
    assert(em.has_component<RigidBodyComponent>(created_root));
    assert(em.has_component<ColliderComponent>(created_root));
    assert(approx_equal(em.get_component<RigidBodyComponent>(created_root).mass, 2.5f));
    assert(em.get_component<RigidBodyComponent>(created_root).body_type == BodyType::Dynamic);

    // Verify child tilemap
    assert(em.has_component<HierarchyComponent>(created_root));
    const auto& children = em.get_component<HierarchyComponent>(created_root).children;
    assert(children.size() == 1);
    EntityID created_tm = children[0];
    assert(em.is_alive(created_tm));
    assert(em.has_component<TilemapComponent>(created_tm));
    const auto& tm_live = em.get_component<TilemapComponent>(created_tm);
    assert(tm_live.tile_count() == 3);
    assert(tm_live.get_tile(0, 0) == 1);
    assert(tm_live.get_tile(1, 0) == 2);
    assert(tm_live.get_tile(2, 0) == 3);
    assert(approx_equal(tm_live.tile_size, 48.0f));
    assert(approx_equal(tm_live.friction, 0.65f));

    // Undo: both entities destroyed
    history.undo();
    assert(!em.is_alive(created_root));
    assert(!em.is_alive(created_tm));

    // Redo: both entities recreated and tilemap/physics reconstructed cleanly
    history.redo();
    assert(em.is_alive(created_root));
    assert(em.has_component<RigidBodyComponent>(created_root));
    assert(approx_equal(em.get_component<RigidBodyComponent>(created_root).mass, 2.5f));
    assert(em.get_component<RigidBodyComponent>(created_root).body_type == BodyType::Dynamic);
    assert(em.has_component<ColliderComponent>(created_root));
    assert(em.get_component<ColliderComponent>(created_root).shapes.size() == 1);
    assert(approx_vec2(em.get_component<ColliderComponent>(created_root).shapes[0].size, glm::vec2(32.0f, 32.0f)));

    const auto& redo_children = em.get_component<HierarchyComponent>(created_root).children;
    assert(redo_children.size() == 1);
    EntityID redo_tm = redo_children[0];
    assert(em.is_alive(redo_tm));
    assert(em.has_component<TilemapComponent>(redo_tm));
    const auto& tm_redo = em.get_component<TilemapComponent>(redo_tm);
    assert(tm_redo.tile_count() == 3);
    assert(tm_redo.get_tile(0, 0) == 1);
    assert(tm_redo.get_tile(1, 0) == 2);
    assert(tm_redo.get_tile(2, 0) == 3);
    assert(approx_equal(tm_redo.tile_size, 48.0f));
    assert(approx_equal(tm_redo.friction, 0.65f));

    std::cout << "test_prefab_physics_and_tilemap_undo_redo passed." << std::endl;
}

void test_prefab_structural_rejections() {
    EntityManager em;
    ComponentSerializationContext ctx;

    // 1. Missing / invalid version
    std::string bad_version = "{\"prefab_version\": 999, \"root_local_id\": 1, \"entities\": [{\"id\": 1}]}";
    auto res1 = EntitySubtreeArchive::instantiate_from_json(em, bad_version, std::nullopt, 0, ctx);
    assert(!res1.success);

    // 2. Duplicate local IDs
    std::string dup_ids = "{\"prefab_version\": 1, \"root_local_id\": 1, \"entities\": [{\"id\": 1}, {\"id\": 1}]}";
    auto res2 = EntitySubtreeArchive::instantiate_from_json(em, dup_ids, std::nullopt, 0, ctx);
    assert(!res2.success);

    // 3. root_local_id not found in entity list
    std::string missing_root = "{\"prefab_version\": 1, \"root_local_id\": 5, \"entities\": [{\"id\": 1}, {\"id\": 2}]}";
    auto res3 = EntitySubtreeArchive::instantiate_from_json(em, missing_root, std::nullopt, 0, ctx);
    assert(!res3.success);

    // 4. Invalid target parent (entity does not exist)
    std::string valid_json = "{\"prefab_version\": 1, \"root_local_id\": 1, \"entities\": [{\"id\": 1, \"transform\": {\"position\": [0,0,0], \"rotation\": 0, \"scale\": [1,1]}}]}";
    auto res4 = EntitySubtreeArchive::instantiate_from_json(em, valid_json, std::nullopt, 99999, ctx);
    assert(!res4.success);

    // 5. Hierarchy cycle among children
    std::string cycle_json = "{\"prefab_version\": 1, \"root_local_id\": 1, \"entities\": ["
        "{\"id\": 1, \"hierarchy\": {\"parent\": 0}},"
        "{\"id\": 2, \"hierarchy\": {\"parent\": 3}},"
        "{\"id\": 3, \"hierarchy\": {\"parent\": 2}}"
        "]}";
    auto res5 = EntitySubtreeArchive::instantiate_from_json(em, cycle_json, std::nullopt, 0, ctx);
    assert(!res5.success);

    std::cout << "test_prefab_structural_rejections passed." << std::endl;
}

void test_instantiate_prefab_drag_drop_scenarios() {
    EntityManager em;
    EditorEntityStateStore state_store;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;

    // Create a minimal test prefab
    EntityID orig_root = em.create_entity();
    em.add_component<TagComponent>(orig_root, TagComponent{"Coin"});
    em.add_component<TransformComponent>(orig_root, TransformComponent{glm::vec3(0.0f)});
    TransformSystem::update(em);

    ComponentSerializationContext ctx;
    std::string json = EntitySubtreeArchive::capture_to_json(em, orig_root, ctx);
    assert(!json.empty());
    em.destroy_entity(orig_root);

    std::filesystem::create_directories("assets/prefabs");
    const std::string prefab_file = "assets/prefabs/coin_dd_test.prefab";
    assert(FileSystem::write_text_atomically(prefab_file, json));

    // --- Scenario 1: Drop at snapped world position ---
    float mouse_x = 123.4f;
    float mouse_y = 456.7f;
    float snap_step = 32.0f;
    float snapped_x = std::round(mouse_x / snap_step) * snap_step; // 128.0f
    float snapped_y = std::round(mouse_y / snap_step) * snap_step; // 448.0f
    TransformComponent desired_tr{glm::vec3(snapped_x, snapped_y, 0.0f)};

    auto cmd1 = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, prefab_file, false, desired_tr, 0, nullptr, &state_store
    );
    assert(cmd1->is_valid());
    history.execute_command(std::move(cmd1));

    assert(selection->count() == 1);
    EntityID spawned_root = selection->get_primary();
    assert(spawned_root != 0);
    assert(em.is_alive(spawned_root));
    const auto& tr1 = em.get_component<TransformComponent>(spawned_root);
    assert(approx_equal(tr1.position.x, 128.0f));
    assert(approx_equal(tr1.position.y, 448.0f));

    // --- Scenario 2: Drop as child under existing parent ---
    EntityID parent_entity = em.create_entity();
    em.add_component<TagComponent>(parent_entity, TagComponent{"Container"});
    em.add_component<TransformComponent>(parent_entity, TransformComponent{glm::vec3(500.0f, 0.0f, 0.0f)});
    TransformSystem::update(em);

    auto cmd2 = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, prefab_file, false, std::nullopt, parent_entity, nullptr, &state_store
    );
    assert(cmd2->is_valid());
    history.execute_command(std::move(cmd2));

    EntityID child_prefab = selection->get_primary();
    assert(child_prefab != 0 && child_prefab != spawned_root);
    assert(em.has_component<HierarchyComponent>(child_prefab));
    assert(em.get_component<HierarchyComponent>(child_prefab).parent == parent_entity);

    // --- Scenario 3: Undo / Redo of drag-instantiated prefabs ---
    assert(history.can_undo());
    history.undo(); // undo child prefab
    assert(!em.is_alive(child_prefab));
    assert(em.is_alive(parent_entity));

    history.undo(); // undo snapped prefab
    assert(!em.is_alive(spawned_root));

    assert(history.can_redo());
    history.redo(); // redo snapped prefab
    assert(em.is_alive(spawned_root));

    history.redo(); // redo child prefab
    assert(em.is_alive(child_prefab));
    assert(em.get_component<HierarchyComponent>(child_prefab).parent == parent_entity);

    // --- Scenario 4: Rejection on locked parent ---
    size_t undo_before = history.get_undo_count();
    state_store.set_locked(parent_entity, true);
    auto cmd_locked = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, prefab_file, false, std::nullopt, parent_entity, nullptr, &state_store
    );
    assert(!cmd_locked->is_valid());
    history.execute_command(std::move(cmd_locked)); // fails cleanly, does not push to history
    assert(history.get_undo_count() == undo_before);

    // --- Scenario 5: Rejection on corrupt/nonexistent file ---
    auto cmd_corrupt = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, "assets/prefabs/nonexistent_xyz.prefab", false, std::nullopt, 0, nullptr, &state_store
    );
    assert(!cmd_corrupt->is_valid());
    history.execute_command(std::move(cmd_corrupt));
    assert(history.get_undo_count() == undo_before);

    // --- Scenario 6: Drop inside Prefab Stage (parents under stage root) ---
    EntityID stage_root = em.create_entity();
    em.add_component<TagComponent>(stage_root, TagComponent{"StageRoot"});
    em.add_component<TransformComponent>(stage_root, TransformComponent{glm::vec3(0.0f)});
    TransformSystem::update(em);

    auto cmd_stage = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, prefab_file, false, std::nullopt, stage_root, nullptr, &state_store
    );
    assert(cmd_stage->is_valid());
    history.execute_command(std::move(cmd_stage));

    EntityID stage_child = selection->get_primary();
    assert(stage_child != 0 && stage_child != stage_root);
    assert(em.has_component<HierarchyComponent>(stage_child));
    assert(em.get_component<HierarchyComponent>(stage_child).parent == stage_root);

    // Undo inside stage removes nested prefab and leaves stage_root untouched
    history.undo();
    assert(!em.is_alive(stage_child));
    assert(em.is_alive(stage_root));

    // Redo inside stage restores nested prefab under stage_root
    history.redo();
    assert(em.is_alive(stage_child));
    assert(em.get_component<HierarchyComponent>(stage_child).parent == stage_root);

    std::filesystem::remove(prefab_file);
    std::cout << "test_instantiate_prefab_drag_drop_scenarios passed." << std::endl;
}

void test_prefab_link_component_and_unpack_revert_commands() {

    EntityManager em;
    EditorEntityStateStore state_store;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;
    ComponentSerializationContext ctx;

    // 1. Test ComponentRegistry serialization of PrefabLinkComponent
    EntityID test_link_entity = em.create_entity();
    em.add_component<TransformComponent>(test_link_entity, TransformComponent{});
    em.add_component<PrefabLinkComponent>(test_link_entity, PrefabLinkComponent{"assets/prefabs/Hero.prefab"});
    nlohmann::json entity_json;
    std::string err;
    assert(default_component_registry().serialize_entity(em, test_link_entity, entity_json, ctx, err));
    assert(entity_json.contains("prefab_link"));
    assert(entity_json["prefab_link"]["asset_path"] == "assets/prefabs/Hero.prefab");
    em.destroy_entity(test_link_entity);




    // 2. Create a 2-entity prefab on disk
    EntityID disk_root = em.create_entity();
    em.add_component<TagComponent>(disk_root, TagComponent{"Goblin_Root"});
    em.add_component<TransformComponent>(disk_root, TransformComponent{glm::vec3(0.0f)});

    EntityID disk_child = em.create_entity();
    em.add_component<TagComponent>(disk_child, TagComponent{"Goblin_Dagger"});
    em.add_component<TransformComponent>(disk_child, TransformComponent{glm::vec3(15.0f, 0.0f, 0.0f)});
    static_cast<void>(em.set_parent(disk_child, disk_root));
    TransformSystem::update(em);

    std::string prefab_json = EntitySubtreeArchive::capture_to_json(em, disk_root, ctx);
    assert(!prefab_json.empty());
    em.destroy_entity(disk_root);

    std::filesystem::create_directories("assets/prefabs");
    const std::string prefab_file = "assets/prefabs/goblin_test.prefab";
    assert(FileSystem::write_text_atomically(prefab_file, prefab_json));

    // 3. Instantiate prefab in scene under a parent container at (100, 200, 0)
    EntityID scene_parent = em.create_entity();
    em.add_component<TagComponent>(scene_parent, TagComponent{"Enemies_Group"});
    em.add_component<TransformComponent>(scene_parent, TransformComponent{glm::vec3(50.0f, 50.0f, 0.0f)});
    TransformSystem::update(em);

    TransformComponent desired_world{glm::vec3(100.0f, 200.0f, 0.0f)};
    auto instantiate_cmd = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, prefab_file, false, desired_world, scene_parent, nullptr, &state_store
    );
    assert(instantiate_cmd->is_valid());
    history.execute_command(std::move(instantiate_cmd));

    EntityID inst_root = selection->get_primary();
    assert(inst_root != 0);
    assert(em.has_component<PrefabLinkComponent>(inst_root));
    assert(em.get_component<PrefabLinkComponent>(inst_root).prefab_asset_path == prefab_file);

    // Child must NOT have PrefabLinkComponent (root only)
    assert(em.has_component<HierarchyComponent>(inst_root));
    const auto& children = em.get_component<HierarchyComponent>(inst_root).children;
    assert(children.size() == 1);
    EntityID inst_child = children.front();
    assert(!em.has_component<PrefabLinkComponent>(inst_child));

    // Verify parentage
    assert(em.get_component<HierarchyComponent>(inst_root).parent == scene_parent);

    // 4. Test Nested Prefab Link preservation during Unpack
    // Attach a separate PrefabLinkComponent to child to simulate nested prefab instance
    em.add_component<PrefabLinkComponent>(inst_child, PrefabLinkComponent{"assets/prefabs/dagger.prefab"});

    auto unpack_cmd = std::make_unique<UnpackPrefabCommand>(&em, selection, inst_root, &state_store);
    assert(unpack_cmd->is_valid());
    history.execute_command(std::move(unpack_cmd));

    // Root link is removed
    assert(!em.has_component<PrefabLinkComponent>(inst_root));
    // Nested child link remains intact
    assert(em.has_component<PrefabLinkComponent>(inst_child));

    // Undo unpack: restores root link
    history.undo();
    assert(em.has_component<PrefabLinkComponent>(inst_root));
    assert(em.get_component<PrefabLinkComponent>(inst_root).prefab_asset_path == prefab_file);

    // Redo unpack: removes root link
    history.redo();
    assert(!em.has_component<PrefabLinkComponent>(inst_root));

    // Undo again so root has PrefabLinkComponent for Revert test
    history.undo();
    assert(em.has_component<PrefabLinkComponent>(inst_root));
    // Remove the fake child link
    em.remove_component<PrefabLinkComponent>(inst_child);

    // 5. Test RevertPrefabCommand
    // Modify instance: rename tag, add a new custom child
    em.get_component<TagComponent>(inst_root).tag = "Goblin_Overridden";
    EntityID extra_child = em.create_entity();
    em.add_component<TagComponent>(extra_child, TagComponent{"Extra_Armor"});
    em.add_component<TransformComponent>(extra_child, TransformComponent{glm::vec3(0.0f)});
    static_cast<void>(em.set_parent(extra_child, inst_root));
    TransformSystem::update(em);

    assert(em.get_component<HierarchyComponent>(inst_root).children.size() == 2);


    auto revert_cmd = std::make_unique<RevertPrefabCommand>(&em, selection, inst_root, nullptr, &state_store);
    assert(revert_cmd->is_valid());
    history.execute_command(std::move(revert_cmd));

    EntityID reverted_root = selection->get_primary();
    assert(reverted_root != 0);
    assert(em.is_alive(reverted_root));

    // Tag restored from disk
    assert(em.get_component<TagComponent>(reverted_root).tag == "Goblin_Root");

    // Children restored to 1 (extra_child destroyed)
    assert(em.has_component<HierarchyComponent>(reverted_root));
    assert(em.get_component<HierarchyComponent>(reverted_root).children.size() == 1);
    assert(!em.is_alive(extra_child));

    // Placement and parent preserved
    assert(em.get_component<HierarchyComponent>(reverted_root).parent == scene_parent);
    const auto& rev_tr = em.get_component<TransformComponent>(reverted_root);
    assert(approx_equal(rev_tr.world_position.x, 100.0f));
    assert(approx_equal(rev_tr.world_position.y, 200.0f));

    // PrefabLinkComponent attached to new root
    assert(em.has_component<PrefabLinkComponent>(reverted_root));
    assert(em.get_component<PrefabLinkComponent>(reverted_root).prefab_asset_path == prefab_file);

    // 6. Test Undo of Revert (restores user overrides & extra_child)
    history.undo();
    EntityID un_revert_root = selection->get_primary();
    assert(em.is_alive(un_revert_root));
    assert(em.get_component<TagComponent>(un_revert_root).tag == "Goblin_Overridden");
    assert(em.get_component<HierarchyComponent>(un_revert_root).children.size() == 2);

    // 7. Test Redo of Revert (re-applies clean asset without disk access)
    history.redo();
    EntityID redo_revert_root = selection->get_primary();
    assert(em.is_alive(redo_revert_root));
    assert(em.get_component<TagComponent>(redo_revert_root).tag == "Goblin_Root");
    assert(em.get_component<HierarchyComponent>(redo_revert_root).children.size() == 1);

    // 8. Revert failure cases
    // Locked entity rejection
    state_store.set_locked(redo_revert_root, true);
    auto revert_locked = std::make_unique<RevertPrefabCommand>(&em, selection, redo_revert_root, nullptr, &state_store);
    assert(!revert_locked->is_valid());
    state_store.set_locked(redo_revert_root, false);

    // Entity without PrefabLinkComponent rejection
    EntityID plain_entity = em.create_entity();
    auto revert_plain = std::make_unique<RevertPrefabCommand>(&em, selection, plain_entity, nullptr, &state_store);
    assert(!revert_plain->is_valid());
    em.destroy_entity(plain_entity);

    std::filesystem::remove(prefab_file);
    std::cout << "test_prefab_link_component_and_unpack_revert_commands passed." << std::endl;
}

void test_revert_prefab_physics_and_atomic_rollback() {
    EntityManager em;
    EditorEntityStateStore state_store;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;
    ComponentSerializationContext ctx;

    // 1. Create physics prefab with RigidBody & Collider
    EntityID phys_root = em.create_entity();
    em.add_component<TagComponent>(phys_root, TagComponent{"Phys_Box"});
    em.add_component<TransformComponent>(phys_root, TransformComponent{glm::vec3(10.0f, 20.0f, 0.0f)});
    em.add_component<RigidBodyComponent>(phys_root, RigidBodyComponent::Dynamic(1.0f, 0.2f, 0.5f, false));
    em.add_component<ColliderComponent>(phys_root, ColliderComponent::Box({32.0f, 32.0f}));
    TransformSystem::update(em);

    std::string phys_json = EntitySubtreeArchive::capture_to_json(em, phys_root, ctx);
    em.destroy_entity(phys_root);

    const std::string phys_prefab_file = "assets/prefabs/phys_revert_test.prefab";
    assert(FileSystem::write_text_atomically(phys_prefab_file, phys_json));

    // Instantiate in scene
    auto inst_cmd = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, phys_prefab_file, false, std::nullopt, 0, nullptr, &state_store
    );
    assert(inst_cmd->is_valid());
    history.execute_command(std::move(inst_cmd));

    EntityID inst_phys = selection->get_primary();
    assert(inst_phys != 0);
    assert(em.has_component<RigidBodyComponent>(inst_phys));
    assert(em.has_component<ColliderComponent>(inst_phys));

    // Execute Revert
    auto rev_phys_cmd = std::make_unique<RevertPrefabCommand>(&em, selection, inst_phys, nullptr, &state_store);
    assert(rev_phys_cmd->is_valid());
    history.execute_command(std::move(rev_phys_cmd));

    EntityID rev_phys = selection->get_primary();
    assert(rev_phys != 0);
    assert(em.has_component<RigidBodyComponent>(rev_phys));
    assert(em.has_component<ColliderComponent>(rev_phys));

    // Undo / Redo cycles
    history.undo();
    EntityID un_phys = selection->get_primary();
    assert(em.is_alive(un_phys));
    assert(em.has_component<RigidBodyComponent>(un_phys));

    history.redo();
    EntityID redo_phys = selection->get_primary();
    assert(em.is_alive(redo_phys));
    assert(em.has_component<RigidBodyComponent>(redo_phys));

    std::filesystem::remove(phys_prefab_file);

    // 2. Test Atomic Rollback on corrupt instantiation with 2-level nested children
    EntityID deep_root = em.create_entity();
    em.add_component<TagComponent>(deep_root, TagComponent{"Deep_Root"});
    em.add_component<TransformComponent>(deep_root, TransformComponent{glm::vec3(0.0f)});
    em.add_component<PrefabLinkComponent>(deep_root, PrefabLinkComponent{"assets/prefabs/corrupt_target.prefab"});

    EntityID deep_child1 = em.create_entity();
    em.add_component<TagComponent>(deep_child1, TagComponent{"Deep_Child1"});
    em.add_component<TransformComponent>(deep_child1, TransformComponent{glm::vec3(5.0f, 0.0f, 0.0f)});
    static_cast<void>(em.set_parent(deep_child1, deep_root));

    EntityID deep_child2 = em.create_entity();
    em.add_component<TagComponent>(deep_child2, TagComponent{"Deep_Child2"});
    em.add_component<TransformComponent>(deep_child2, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f)});
    static_cast<void>(em.set_parent(deep_child2, deep_child1));
    TransformSystem::update(em);

    // Create an invalid asset file (syntactically valid JSON array but missing root)
    const std::string corrupt_file = "assets/prefabs/corrupt_target.prefab";
    assert(FileSystem::write_text_atomically(corrupt_file, R"({"entities": []})"));

    auto bad_revert = std::make_unique<RevertPrefabCommand>(&em, selection, deep_root, nullptr, &state_store);
    assert(bad_revert->is_valid());
    assert(!bad_revert->execute()); // execute fails and triggers rollback

    // Verify all 3 entities were safely restored and parent hierarchy is intact
    EntityID restored_root = selection->get_primary();
    assert(restored_root != 0);
    assert(em.is_alive(restored_root));
    assert(em.get_component<TagComponent>(restored_root).tag == "Deep_Root");
    assert(em.has_component<HierarchyComponent>(restored_root));
    assert(em.get_component<HierarchyComponent>(restored_root).children.size() == 1);

    EntityID res_child1 = em.get_component<HierarchyComponent>(restored_root).children.front();
    assert(em.is_alive(res_child1));
    assert(em.get_component<TagComponent>(res_child1).tag == "Deep_Child1");
    assert(em.get_component<HierarchyComponent>(res_child1).parent == restored_root);
    assert(em.get_component<HierarchyComponent>(res_child1).children.size() == 1);

    EntityID res_child2 = em.get_component<HierarchyComponent>(res_child1).children.front();
    assert(em.is_alive(res_child2));
    assert(em.get_component<TagComponent>(res_child2).tag == "Deep_Child2");
    assert(em.get_component<HierarchyComponent>(res_child2).parent == res_child1);

    std::filesystem::remove(corrupt_file);
    std::cout << "test_revert_prefab_physics_and_atomic_rollback passed." << std::endl;
}

void test_apply_prefab_command_and_undo_redo() {
    EntityManager em;
    EditorEntityStateStore state_store;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;
    ComponentSerializationContext ctx;

    // 1. Create a baseline 1-entity prefab on disk
    EntityID initial_root = em.create_entity();
    em.add_component<TagComponent>(initial_root, TagComponent{"Warrior_Base"});
    em.add_component<TransformComponent>(initial_root, TransformComponent{glm::vec3(0.0f)});
    TransformSystem::update(em);

    std::string base_json = EntitySubtreeArchive::capture_to_json(em, initial_root, ctx);
    em.destroy_entity(initial_root);

    const std::string prefab_file = "assets/prefabs/warrior_apply_test.prefab";
    assert(FileSystem::write_text_atomically(prefab_file, base_json));

    // 2. Instantiate in scene
    auto inst_cmd = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, prefab_file, false, std::nullopt, 0, nullptr, &state_store
    );
    assert(inst_cmd->is_valid());
    history.execute_command(std::move(inst_cmd));

    EntityID inst_root = selection->get_primary();
    assert(inst_root != 0);
    assert(em.has_component<PrefabLinkComponent>(inst_root));

    // 3. Modify instance in scene: change tag, add child entity
    em.get_component<TagComponent>(inst_root).tag = "Warrior_Upgraded";
    EntityID shield_child = em.create_entity();
    em.add_component<TagComponent>(shield_child, TagComponent{"Shield_Item"});
    em.add_component<TransformComponent>(shield_child, TransformComponent{glm::vec3(12.0f, 0.0f, 0.0f)});
    static_cast<void>(em.set_parent(shield_child, inst_root));
    TransformSystem::update(em);

    // 4. Execute ApplyPrefabCommand
    auto apply_cmd = std::make_unique<ApplyPrefabCommand>(&em, inst_root, nullptr, &state_store);
    assert(apply_cmd->is_valid());
    history.execute_command(std::move(apply_cmd));

    // Check that disk file was updated with new structure and stripped prefab_link
    std::string updated_disk_json = PrefabSerializer::read_json_from_file(prefab_file);
    assert(!updated_disk_json.empty());
    assert(updated_disk_json.find("Warrior_Upgraded") != std::string::npos);
    assert(updated_disk_json.find("Shield_Item") != std::string::npos);
    assert(updated_disk_json.find("prefab_link") == std::string::npos); // Strip self-link

    // 5. Test Undo (restores previous disk file)
    history.undo();
    std::string undone_disk_json = PrefabSerializer::read_json_from_file(prefab_file);
    assert(undone_disk_json.find("Warrior_Base") != std::string::npos);
    assert(undone_disk_json.find("Shield_Item") == std::string::npos);

    // 6. Test Redo (re-applies updated disk file)
    history.redo();
    std::string redone_disk_json = PrefabSerializer::read_json_from_file(prefab_file);
    assert(redone_disk_json.find("Warrior_Upgraded") != std::string::npos);
    assert(redone_disk_json.find("Shield_Item") != std::string::npos);

    // 7. Error handling: locked entity
    state_store.set_locked(inst_root, true);
    auto apply_locked = std::make_unique<ApplyPrefabCommand>(&em, inst_root, nullptr, &state_store);
    assert(!apply_locked->is_valid());
    state_store.set_locked(inst_root, false);

    // Error handling: entity without PrefabLinkComponent
    EntityID plain_e = em.create_entity();
    auto apply_plain = std::make_unique<ApplyPrefabCommand>(&em, plain_e, nullptr, &state_store);
    assert(!apply_plain->is_valid());
    em.destroy_entity(plain_e);

    std::filesystem::remove(prefab_file);
    std::cout << "test_apply_prefab_command_and_undo_redo passed." << std::endl;
}

void test_prefab_live_propagation_on_apply() {
    EntityManager em;
    EditorEntityStateStore state_store;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;
    ComponentSerializationContext ctx;

    // 1. Create a baseline prefab
    EntityID initial_root = em.create_entity();
    em.add_component<TagComponent>(initial_root, TagComponent{"Prop_Coin"});
    em.add_component<TransformComponent>(initial_root, TransformComponent{glm::vec3(0.0f)});
    TransformSystem::update(em);

    const std::string prefab_file = "assets/prefabs/prop_coin_test.prefab";
    std::string base_json = EntitySubtreeArchive::capture_to_json(em, initial_root, ctx);
    em.destroy_entity(initial_root);
    assert(FileSystem::write_text_atomically(prefab_file, base_json));

    // 2. Instantiate 2 separate instances in scene at different positions
    auto inst1 = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, prefab_file, false, TransformComponent{glm::vec3(100.0f, 200.0f, 0.0f)}, 0, nullptr, &state_store
    );
    history.execute_command(std::move(inst1));
    EntityID root1 = selection->get_primary();
    assert(root1 != 0);

    auto inst2 = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, prefab_file, false, TransformComponent{glm::vec3(500.0f, 600.0f, 0.0f)}, 0, nullptr, &state_store
    );
    history.execute_command(std::move(inst2));
    EntityID root2 = selection->get_primary();
    assert(root2 != 0);
    assert(root1 != root2);

    // 3. Modify instance 1 (add a Sparkle child) and apply to asset
    EntityID sparkle = em.create_entity();
    em.add_component<TagComponent>(sparkle, TagComponent{"Sparkle"});
    em.add_component<TransformComponent>(sparkle, TransformComponent{glm::vec3(5.0f, 5.0f, 0.0f)});
    static_cast<void>(em.set_parent(sparkle, root1));
    TransformSystem::update(em);

    auto apply_cmd = std::make_unique<ApplyPrefabCommand>(&em, root1, nullptr, &state_store);
    assert(apply_cmd->is_valid());
    history.execute_command(std::move(apply_cmd));

    // 4. Verify instance 2 was automatically updated live with the Sparkle child,
    // but retained its own position at (500, 600)!
    std::vector<EntityID> coin_roots;
    em.for_each<PrefabLinkComponent>([&](EntityID id, const PrefabLinkComponent& link) {
        if (link.prefab_asset_path == prefab_file) {
            coin_roots.push_back(id);
        }
    });
    assert(coin_roots.size() == 2);

    // Find the second instance (which has position 500, 600)
    EntityID other_inst = (coin_roots[0] == root1) ? coin_roots[1] : coin_roots[0];
    assert(em.is_alive(other_inst));
    assert(approx_vec3(em.get_component<TransformComponent>(other_inst).world_position, glm::vec3(500.0f, 600.0f, 0.0f)));
    assert(em.has_component<HierarchyComponent>(other_inst));
    assert(em.get_component<HierarchyComponent>(other_inst).children.size() == 1);
    EntityID child_of_other = em.get_component<HierarchyComponent>(other_inst).children.front();
    assert(em.get_component<TagComponent>(child_of_other).tag == "Sparkle");

    // 5. Test Undo: restores both instances to having 0 children
    history.undo();
    for (EntityID id : coin_roots) {
        if (em.is_alive(id) && em.has_component<HierarchyComponent>(id)) {
            assert(em.get_component<HierarchyComponent>(id).children.empty());
        }
    }

    std::filesystem::remove(prefab_file);
    std::cout << "test_prefab_live_propagation_on_apply passed." << std::endl;
}

void test_prefab_duplication_preserves_link() {
    EntityManager em;
    EditorEntityStateStore state_store;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;
    ComponentSerializationContext ctx;

    EntityID initial_root = em.create_entity();
    em.add_component<TagComponent>(initial_root, TagComponent{"Turret"});
    em.add_component<TransformComponent>(initial_root, TransformComponent{glm::vec3(50.0f, 50.0f, 0.0f)});
    TransformSystem::update(em);

    const std::string prefab_file = "assets/prefabs/turret_dup_test.prefab";
    std::string base_json = EntitySubtreeArchive::capture_to_json(em, initial_root, ctx);
    em.destroy_entity(initial_root);
    assert(FileSystem::write_text_atomically(prefab_file, base_json));

    auto inst = std::make_unique<InstantiatePrefabCommand>(
        &em, selection, prefab_file, false, std::nullopt, 0, nullptr, &state_store
    );
    history.execute_command(std::move(inst));
    EntityID original = selection->get_primary();
    assert(original != 0);
    assert(em.has_component<PrefabLinkComponent>(original));

    // Duplicate the instance
    auto dup_cmd = std::make_unique<DuplicateEntitiesCommand>(
        &em, selection, std::vector<EntityID>{original}, &state_store
    );
    assert(dup_cmd->is_valid());
    history.execute_command(std::move(dup_cmd));

    EntityID clone = selection->get_primary();
    assert(clone != 0);
    assert(clone != original);
    assert(em.has_component<PrefabLinkComponent>(clone));
    assert(em.get_component<PrefabLinkComponent>(clone).prefab_asset_path == prefab_file);

    // Unpack clone only; original should still have link
    auto unpack_clone = std::make_unique<UnpackPrefabCommand>(&em, selection, clone, &state_store);
    assert(unpack_clone->is_valid());
    history.execute_command(std::move(unpack_clone));

    assert(!em.has_component<PrefabLinkComponent>(clone));
    assert(em.has_component<PrefabLinkComponent>(original));

    std::filesystem::remove(prefab_file);
    std::cout << "test_prefab_duplication_preserves_link passed." << std::endl;
}

void test_prefab_multi_selection_batch_unpack() {
    EntityManager em;
    EditorEntityStateStore state_store;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;
    ComponentSerializationContext ctx;

    EntityID initial_root = em.create_entity();
    em.add_component<TagComponent>(initial_root, TagComponent{"Bush"});
    em.add_component<TransformComponent>(initial_root, TransformComponent{glm::vec3(0.0f)});
    TransformSystem::update(em);

    const std::string prefab_file = "assets/prefabs/bush_multi_test.prefab";
    std::string base_json = EntitySubtreeArchive::capture_to_json(em, initial_root, ctx);
    em.destroy_entity(initial_root);
    assert(FileSystem::write_text_atomically(prefab_file, base_json));

    auto inst1 = std::make_unique<InstantiatePrefabCommand>(&em, selection, prefab_file, false, std::nullopt, 0, nullptr, &state_store);
    history.execute_command(std::move(inst1));
    EntityID b1 = selection->get_primary();

    auto inst2 = std::make_unique<InstantiatePrefabCommand>(&em, selection, prefab_file, false, std::nullopt, 0, nullptr, &state_store);
    history.execute_command(std::move(inst2));
    EntityID b2 = selection->get_primary();

    assert(em.has_component<PrefabLinkComponent>(b1));
    assert(em.has_component<PrefabLinkComponent>(b2));

    // Batch unpack
    std::vector<EntityID> batch{b1, b2};
    for (EntityID id : batch) {
        auto unpack_cmd = std::make_unique<UnpackPrefabCommand>(&em, selection, id, &state_store);
        history.execute_command(std::move(unpack_cmd));
    }

    assert(!em.has_component<PrefabLinkComponent>(b1));
    assert(!em.has_component<PrefabLinkComponent>(b2));

    // Undo both unpacks
    history.undo();
    history.undo();
    assert(em.has_component<PrefabLinkComponent>(b1));
    assert(em.has_component<PrefabLinkComponent>(b2));

    std::filesystem::remove(prefab_file);
    std::cout << "test_prefab_multi_selection_batch_unpack passed." << std::endl;
}

void test_prefab_property_overrides_detection() {
    EntityManager em;
    EditorEntityStateStore state_store;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;

    // Create prefab with Transform and Sprite
    EntityID orig = em.create_entity();
    em.add_component<TagComponent>(orig, TagComponent{"Enemy"});
    em.add_component<TransformComponent>(orig, TransformComponent{glm::vec3(10.0f, 20.0f, 0.0f), glm::vec2(1.0f, 1.0f), 0.0f});
    em.add_component<SpriteComponent>(orig, SpriteComponent{nullptr, glm::vec2(32.0f, 32.0f), SDL_Color{255, 255, 255, 255}});
    TransformSystem::update(em);

    std::filesystem::create_directories("assets/prefabs");
    const std::string prefab_file = "assets/prefabs/override_detection_test.prefab";
    assert(PrefabSerializer::save_to_file(em, orig, prefab_file, nullptr));
    em.destroy_entity(orig);

    // Instantiate prefab instance into scene
    auto inst_cmd = std::make_unique<InstantiatePrefabCommand>(&em, selection, prefab_file, false, std::nullopt, 0, nullptr, &state_store);
    history.execute_command(std::move(inst_cmd));
    EntityID instance = selection->get_primary();
    assert(instance != 0);

    // 1. Initially (placed in scene, position may differ), no non-position overrides exist
    auto initial_overrides = PrefabOverrides::get_overridden_components(em, instance);
    assert(initial_overrides.empty());
    assert(!PrefabOverrides::is_component_overridden(em, instance, "sprite"));
    assert(!PrefabOverrides::is_component_overridden(em, instance, "transform"));

    // Changing scene position does NOT trigger transform override for root entity
    em.get_component<TransformComponent>(instance).position = glm::vec3(500.0f, 500.0f, 0.0f);
    assert(!PrefabOverrides::is_component_overridden(em, instance, "transform"));

    // 2. Modify sprite color -> sprite override detected!
    em.get_component<SpriteComponent>(instance).color = SDL_Color{255, 0, 0, 255};
    assert(PrefabOverrides::is_component_overridden(em, instance, "sprite"));
    assert(!PrefabOverrides::is_component_overridden(em, instance, "transform"));
    auto sprite_overrides = PrefabOverrides::get_overridden_components(em, instance);
    assert(sprite_overrides.size() == 1);
    assert(sprite_overrides[0] == "sprite");

    // 3. Modify transform scale -> both sprite and transform overrides detected!
    em.get_component<TransformComponent>(instance).scale = glm::vec2(2.5f, 2.5f);
    assert(PrefabOverrides::is_component_overridden(em, instance, "transform"));
    auto multi_overrides = PrefabOverrides::get_overridden_components(em, instance);
    assert(multi_overrides.size() == 2);

    std::filesystem::remove(prefab_file);
    std::cout << "test_prefab_property_overrides_detection passed." << std::endl;
}

void test_granular_revert_and_apply_component_command() {
    EntityManager em;
    EditorEntityStateStore state_store;
    auto selection = std::make_shared<EditorSelection>();
    EditorCommandHistory history;

    // Create master prefab
    EntityID orig = em.create_entity();
    em.add_component<TagComponent>(orig, TagComponent{"Player"});
    em.add_component<TransformComponent>(orig, TransformComponent{glm::vec3(0.0f), glm::vec2(1.0f, 1.0f), 0.0f});
    em.add_component<SpriteComponent>(orig, SpriteComponent{nullptr, glm::vec2(32.0f, 32.0f), SDL_Color{255, 255, 255, 255}});
    em.add_component<RigidBodyComponent>(orig, RigidBodyComponent{BodyType::Dynamic, 1.0f, 1.0f});
    TransformSystem::update(em);

    std::filesystem::create_directories("assets/prefabs");
    const std::string prefab_file = "assets/prefabs/granular_cmd_test.prefab";
    assert(PrefabSerializer::save_to_file(em, orig, prefab_file, nullptr));
    em.destroy_entity(orig);

    // Instantiate prefab
    auto inst_cmd = std::make_unique<InstantiatePrefabCommand>(&em, selection, prefab_file, false, std::nullopt, 0, nullptr, &state_store);
    history.execute_command(std::move(inst_cmd));
    EntityID instance = selection->get_primary();
    assert(instance != 0);

    // Modify sprite and rigidbody
    em.get_component<SpriteComponent>(instance).color = SDL_Color{0, 255, 0, 255};
    em.get_component<RigidBodyComponent>(instance).gravity_scale = 5.0f;

    assert(PrefabOverrides::is_component_overridden(em, instance, "sprite"));
    assert(PrefabOverrides::is_component_overridden(em, instance, "rigidbody"));

    // --- 1. Granular Revert: Revert ONLY sprite ---
    auto revert_sprite = std::make_unique<RevertComponentPrefabCommand>(&em, instance, "sprite", nullptr, &state_store);
    history.execute_command(std::move(revert_sprite));

    // Sprite color reverted back to 255,255,255
    const auto& s = em.get_component<SpriteComponent>(instance);
    assert(s.color.r == 255 && s.color.g == 255 && s.color.b == 255);
    assert(!PrefabOverrides::is_component_overridden(em, instance, "sprite"));
    // RigidBody is STILL overridden (gravity_scale = 5.0)
    assert(PrefabOverrides::is_component_overridden(em, instance, "rigidbody"));
    assert(em.get_component<RigidBodyComponent>(instance).gravity_scale == 5.0f);

    // Undo sprite revert
    history.undo();
    assert(em.get_component<SpriteComponent>(instance).color.g == 255 && em.get_component<SpriteComponent>(instance).color.r == 0);
    assert(PrefabOverrides::is_component_overridden(em, instance, "sprite"));

    // Redo sprite revert
    history.redo();
    assert(em.get_component<SpriteComponent>(instance).color.r == 255 && em.get_component<SpriteComponent>(instance).color.g == 255);

    // --- 2. Granular Apply: Apply ONLY RigidBody modifications to asset ---
    auto apply_rb = std::make_unique<ApplyComponentPrefabCommand>(&em, instance, "rigidbody", nullptr, &state_store);
    history.execute_command(std::move(apply_rb));

    // RigidBody override is now cleared because disk asset matches current instance!
    assert(!PrefabOverrides::is_component_overridden(em, instance, "rigidbody"));

    // Instantiate a second instance from updated asset to verify disk was updated with gravity_scale 5.0
    auto inst2_cmd = std::make_unique<InstantiatePrefabCommand>(&em, selection, prefab_file, false, std::nullopt, 0, nullptr, &state_store);
    history.execute_command(std::move(inst2_cmd));
    EntityID instance2 = selection->get_primary();
    assert(em.get_component<RigidBodyComponent>(instance2).gravity_scale == 5.0f);
    // And Sprite in asset remains uncorrupted white
    assert(em.get_component<SpriteComponent>(instance2).color.r == 255);

    std::filesystem::remove(prefab_file);
    std::cout << "test_granular_revert_and_apply_component_command passed." << std::endl;
}

void test_nested_prefab_serialization_and_instantiation() {
    std::string child_prefab_file = "temp_nested_child.prefab";
    std::string parent_prefab_file = "temp_nested_parent.prefab";

    // 1. Create and save child prefab
    EntityManager child_em;
    EntityID child_root = child_em.create_entity();
    child_em.add_component<TagComponent>(child_root, TagComponent{"Child_Weapon"});
    child_em.add_component<TransformComponent>(child_root, TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});
    child_em.add_component<SpriteComponent>(child_root, SpriteComponent{});
    assert(PrefabSerializer::save_to_file(child_em, child_root, child_prefab_file));

    // 2. Create parent entity and attach child as a nested prefab instance
    EntityManager parent_em;
    EntityID p_root = parent_em.create_entity();
    parent_em.add_component<TagComponent>(p_root, TagComponent{"Parent_Character"});
    parent_em.add_component<TransformComponent>(p_root, TransformComponent{glm::vec3(10.0f, 20.0f, 0.0f)});

    EntityID p_nested_child = parent_em.create_entity();
    parent_em.add_component<TagComponent>(p_nested_child, TagComponent{"Equipped_Weapon"});
    parent_em.add_component<TransformComponent>(p_nested_child, TransformComponent{glm::vec3(2.0f, 1.0f, 0.0f)});
    parent_em.add_component<SpriteComponent>(p_nested_child, SpriteComponent{});
    parent_em.add_component<PrefabLinkComponent>(p_nested_child, PrefabLinkComponent{child_prefab_file});
    assert(parent_em.set_parent(p_nested_child, p_root).succeeded());

    // 3. Save parent prefab
    assert(PrefabSerializer::save_to_file(parent_em, p_root, parent_prefab_file));

    // 4. Instantiate parent prefab into a new scene
    EntityManager scene_em;
    ComponentSerializationContext ctx;
    std::string json = PrefabSerializer::read_json_from_file(parent_prefab_file);
    auto res = EntitySubtreeArchive::instantiate_from_json(scene_em, json, std::nullopt, 0, ctx);
    assert(res.success);
    assert(res.created_entities.size() == 2);

    EntityID inst_root = res.root_entity;
    assert(scene_em.has_component<HierarchyComponent>(inst_root));
    const auto& children = scene_em.get_component<HierarchyComponent>(inst_root).children;
    assert(children.size() == 1);

    EntityID inst_nested = children[0];
    assert(scene_em.has_component<PrefabLinkComponent>(inst_nested));
    assert(scene_em.get_component<PrefabLinkComponent>(inst_nested).prefab_asset_path == child_prefab_file);

    std::filesystem::remove(child_prefab_file);
    std::filesystem::remove(parent_prefab_file);
    std::cout << "test_nested_prefab_serialization_and_instantiation passed." << std::endl;
}

void test_nested_prefab_cycle_rejection() {
    std::string pA = "temp_cycle_A.prefab";
    std::string pB = "temp_cycle_B.prefab";

    // Direct self-nesting check
    assert(EntitySubtreeArchive::has_prefab_nesting_cycle(pA, pA));

    // Create A containing link to B
    EntityManager emA;
    EntityID rootA = emA.create_entity();
    emA.add_component<TransformComponent>(rootA, TransformComponent{});
    EntityID childA = emA.create_entity();
    emA.add_component<TransformComponent>(childA, TransformComponent{});
    emA.add_component<PrefabLinkComponent>(childA, PrefabLinkComponent{pB});
    assert(emA.set_parent(childA, rootA).succeeded());

    assert(PrefabSerializer::save_to_file(emA, rootA, pA));

    // If B tries to nest A, it must detect the cycle!
    assert(EntitySubtreeArchive::has_prefab_nesting_cycle(pB, pA));

    std::filesystem::remove(pA);
    std::filesystem::remove(pB);
    std::cout << "test_nested_prefab_cycle_rejection passed." << std::endl;
}

void test_prefab_variant_creation_and_command_undo_redo() {
    std::string base_file = "temp_base_enemy.prefab";
    std::string variant_file = "temp_variant_enemy.prefab";

    // 1. Create Base Prefab
    EntityManager em;
    EntityID base_entity = em.create_entity();
    em.add_component<TagComponent>(base_entity, TagComponent{"BaseEnemy"});
    em.add_component<TransformComponent>(base_entity, TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});
    SpriteComponent base_sprite;
    base_sprite.color = SDL_Color{255, 0, 0, 255};
    em.add_component<SpriteComponent>(base_entity, base_sprite);
    assert(PrefabSerializer::save_to_file(em, base_entity, base_file));

    // 2. Instantiate base prefab into scene
    em.add_component<PrefabLinkComponent>(base_entity, PrefabLinkComponent{base_file});

    // 3. Create Variant Command
    EditorCommandHistory history;
    auto create_var_cmd = std::make_unique<CreatePrefabVariantCommand>(&em, base_entity, variant_file);
    assert(create_var_cmd->is_valid());
    history.execute_command(std::move(create_var_cmd));

    // Verify entity is now marked as a variant
    assert(em.has_component<PrefabLinkComponent>(base_entity));
    const auto& var_link = em.get_component<PrefabLinkComponent>(base_entity);
    assert(var_link.is_variant());
    assert(var_link.prefab_asset_path == variant_file);
    assert(var_link.base_prefab_path == base_file);

    // Verify file on disk is recognized as variant
    std::string detected_base;
    assert(PrefabSerializer::is_variant_file(variant_file, &detected_base));
    assert(detected_base == base_file);

    // 4. Test Undo
    assert(history.can_undo());
    history.undo();
    const auto& reverted_link = em.get_component<PrefabLinkComponent>(base_entity);
    assert(!reverted_link.is_variant());
    assert(reverted_link.prefab_asset_path == base_file);

    // 5. Test Redo
    assert(history.can_redo());
    history.redo();
    const auto& redone_link = em.get_component<PrefabLinkComponent>(base_entity);
    assert(redone_link.is_variant());
    assert(redone_link.prefab_asset_path == variant_file);
    assert(redone_link.base_prefab_path == base_file);

    // 6. Test Undo then create variant again (file already exists on disk)
    history.undo();
    assert(!em.get_component<PrefabLinkComponent>(base_entity).is_variant());
    auto create_var_cmd2 = std::make_unique<CreatePrefabVariantCommand>(&em, base_entity, variant_file);
    assert(create_var_cmd2->is_valid());
    history.execute_command(std::move(create_var_cmd2));
    assert(em.get_component<PrefabLinkComponent>(base_entity).is_variant());

    // 7. Test Variant Instantiation from JSON
    EntityManager scene_em;
    ComponentSerializationContext ctx;
    std::string var_json = PrefabSerializer::read_json_from_file(variant_file);
    auto inst_res = EntitySubtreeArchive::instantiate_from_json(scene_em, var_json, std::nullopt, 0, ctx);
    assert(inst_res.success);
    assert(inst_res.base_prefab_path == base_file);

    std::filesystem::remove(base_file);
    std::filesystem::remove(variant_file);
    std::cout << "test_prefab_variant_creation_and_command_undo_redo passed." << std::endl;
}



int main() {
    std::cout << "=== Running Prefab System Tests ===" << std::endl;
    test_prefab_serialization_and_deserialization_round_trip();
    test_prefab_subtree_preserves_geometry_from_rotated_scaled_parent();
    test_prefab_internal_entity_reference_remapping();
    test_prefab_external_entity_reference_cleared();
    test_prefab_instantiation_under_transformed_parent();
    test_prefab_atomic_rollback_on_corrupt_data();
    test_instantiate_prefab_command_undo_redo_without_disk_dependency();
    test_prefab_physics_and_tilemap_undo_redo();
    test_prefab_structural_rejections();
    test_instantiate_prefab_drag_drop_scenarios();
    test_prefab_link_component_and_unpack_revert_commands();
    test_revert_prefab_physics_and_atomic_rollback();
    test_apply_prefab_command_and_undo_redo();
    test_prefab_live_propagation_on_apply();
    test_prefab_duplication_preserves_link();
    test_prefab_multi_selection_batch_unpack();
    test_prefab_property_overrides_detection();
    test_granular_revert_and_apply_component_command();
    test_nested_prefab_serialization_and_instantiation();
    test_nested_prefab_cycle_rejection();
    test_prefab_variant_creation_and_command_undo_redo();
    std::cout << "=== All Prefab System Tests Passed! ===" << std::endl;
    return 0;
}


