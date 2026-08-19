#include "scene/EntitySubtreeArchive.hpp"
#include "scene/PrefabSerializer.hpp"
#include "editor/commands/PrefabCommands.hpp"
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
    std::cout << "=== All Prefab System Tests Passed! ===" << std::endl;
    return 0;
}
